/**
 * File purpose: Render the checked-in LVGL dashboard into a 480 by 272 memory framebuffer
 * and export deterministic RGB captures; no display, touch, CAN or storage device is used.
 */
#include "render.h"

#include <stdio.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "ui/ui.h"

#define DASHBOARD_RENDER_WIDTH 480U
#define DASHBOARD_RENDER_HEIGHT 272U
#define DASHBOARD_RENDER_BUFFER_ROWS 32U

static lv_color_t dashboard_framebuffer[DASHBOARD_RENDER_WIDTH * DASHBOARD_RENDER_HEIGHT];
static lv_color_t dashboard_draw_buffer[DASHBOARD_RENDER_WIDTH * DASHBOARD_RENDER_BUFFER_ROWS];
static lv_disp_draw_buf_t dashboard_draw_buffer_descriptor;
static lv_disp_drv_t dashboard_display_driver;
static lv_disp_t *dashboard_display = NULL;
static uint32_t dashboard_last_tick_ms = 0U;

/**
 * Copy the LVGL area-sized pixel buffer into matching framebuffer coordinates, clipping
 * any out-of-display coordinates while retaining the area's source stride. Complete the
 * synchronous flush only after all included rows are copied.
 *
 * Inputs: driver is the display issuing flush; area has inclusive pixel bounds and colors
 * has row stride equal to the original area width. Returns nothing; copies only visible
 * destinations while retaining source indexing, then calls lv_disp_flush_ready. The copy
 * is synchronous; no DMA completion is modeled.
 */
static void
dashboard_render_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *colors) {
  int32_t area_width = (int32_t)area->x2 - (int32_t)area->x1 + 1;
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    for (int32_t x = area->x1; x <= area->x2; ++x) {
      if (x >= 0 && x < (int32_t)DASHBOARD_RENDER_WIDTH && y >= 0 &&
          y < (int32_t)DASHBOARD_RENDER_HEIGHT) {
        size_t destination = (size_t)y * DASHBOARD_RENDER_WIDTH + (size_t)x;
        size_t source = (size_t)(y - area->y1) * (size_t)area_width + (size_t)(x - area->x1);
        dashboard_framebuffer[destination] = colors[source];
      }
    }
  }
  lv_disp_flush_ready(driver);
}

/**
 * Register a partial-buffer memory display and create the existing main/debug screens.
 * Repeated calls while active succeed without creating duplicate displays or objects.
 *
 * Inputs: none. Returns true for an already registered display or a newly registered one;
 * false if registration fails. On first entry lv_init, buffers/tick reset, draw-buffer
 * registration and ui_init precede the first refresh. The partial buffer is 480x32 pixels,
 * while the retained framebuffer is 480x272.
 */
bool dashboard_render_init(void) {
  if (dashboard_display != NULL) {
    return true;
  }
  lv_init();
  memset(dashboard_framebuffer, 0, sizeof(dashboard_framebuffer));
  dashboard_last_tick_ms = 0U;
  lv_disp_draw_buf_init(&dashboard_draw_buffer_descriptor,
                        dashboard_draw_buffer,
                        NULL,
                        DASHBOARD_RENDER_WIDTH * DASHBOARD_RENDER_BUFFER_ROWS);
  lv_disp_drv_init(&dashboard_display_driver);
  dashboard_display_driver.hor_res = DASHBOARD_RENDER_WIDTH;
  dashboard_display_driver.ver_res = DASHBOARD_RENDER_HEIGHT;
  dashboard_display_driver.draw_buf = &dashboard_draw_buffer_descriptor;
  dashboard_display_driver.flush_cb = dashboard_render_flush;
  dashboard_display = lv_disp_drv_register(&dashboard_display_driver);
  if (dashboard_display == NULL) {
    return false;
  }
  ui_init();
  lv_refr_now(dashboard_display);
  return true;
}

/**
 * Advance the LVGL tick by the unsigned difference from the last simulated timestamp and
 * process timers. The caller controls time; this function does not wait on a wall clock.
 *
 * Input: now_ms is absolute uint32 simulated time. Returns nothing; null display is a no-
 * op. LVGL receives the unsigned elapsed difference, then timer handling runs; monotonic
 * time (or intentional rollover) is the caller's responsibility.
 */
void dashboard_render_step(uint32_t now_ms) {
  if (dashboard_display == NULL) {
    return;
  }
  lv_tick_inc(now_ms - dashboard_last_tick_ms);
  dashboard_last_tick_ms = now_ms;
  (void)lv_timer_handler();
}

/**
 * Refresh the current display and serialize each framebuffer RGB565 color through LVGL's
 * own color conversion to RGB bytes. Return false for invalid paths or file errors.
 *
 * Input: ppm_path is an ordinary output path. Returns false before refresh for a missing
 * display/null path, or on open/header/row/close failure. On success produces P6 header
 * plus 480*272*3 RGB bytes, using lv_color_to32 from the configured RGB565 colors. It does
 * not reinterpret CAN values or synthesize browser graphics.
 */
bool dashboard_render_capture(const char *ppm_path) {
  if (dashboard_display == NULL || ppm_path == NULL) {
    return false;
  }
  lv_refr_now(dashboard_display);
  FILE *output = fopen(ppm_path, "wb");
  if (output == NULL) {
    return false;
  }
  bool success = fprintf(output,
                         "P6\n%u %u\n255\n",
                         (unsigned int)DASHBOARD_RENDER_WIDTH,
                         (unsigned int)DASHBOARD_RENDER_HEIGHT) > 0;
  uint8_t row[DASHBOARD_RENDER_WIDTH * 3U];
  for (uint32_t y = 0U; success && y < DASHBOARD_RENDER_HEIGHT; ++y) {
    for (uint32_t x = 0U; x < DASHBOARD_RENDER_WIDTH; ++x) {
      uint32_t color = lv_color_to32(dashboard_framebuffer[y * DASHBOARD_RENDER_WIDTH + x]);
      row[x * 3U] = (uint8_t)(color >> 16U);
      row[x * 3U + 1U] = (uint8_t)(color >> 8U);
      row[x * 3U + 2U] = (uint8_t)color;
    }
    success = fwrite(row, 1U, sizeof(row), output) == sizeof(row);
  }
  if (fclose(output) != 0) {
    success = false;
  }
  return success;
}

/**
 * Tear down generated UI objects and remove the memory display and its remaining screen
 * trees. LVGL's custom-allocation configuration keeps its global core initialized.
 *
 * Inputs: none. Returns nothing; null display is a no-op. Generated main/debug deletion,
 * separate initial-actions deletion and display removal precede clearing the registered
 * display handle. Shared global LVGL memory/core initialization is retained; no board
 * peripherals are touched.
 */
void dashboard_render_shutdown(void) {
  if (dashboard_display == NULL) {
    return;
  }
  ui_destroy();
  if (ui____initial_actions0 != NULL) {
    lv_obj_del(ui____initial_actions0);
    ui____initial_actions0 = NULL;
  }
  lv_disp_remove(dashboard_display);
  dashboard_display = NULL;
}
