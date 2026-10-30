/**
 * File purpose: Declare the memory-backed LVGL display used for deterministic host
 * rendering of the checked-in dashboard screens without an SDL or hardware backend.
 */
#ifndef DASHBOARD_RENDER_H
#define DASHBOARD_RENDER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Create a 480 by 272 memory display and initialize the actual generated UI.
 *
 * Inputs: none. Returns true for an already registered display or a newly registered one;
 * false if registration fails. On first entry lv_init, buffers/tick reset, draw-buffer
 * registration and ui_init precede the first refresh. The partial buffer is 480x32 pixels,
 * while the retained framebuffer is 480x272.
 */
bool dashboard_render_init(void);
/**
 * Advance LVGL to the absolute simulated millisecond tick, including unsigned rollover.
 *
 * Input: now_ms is absolute uint32 simulated time. Returns nothing; null display is a no-
 * op. LVGL receives the unsigned elapsed difference, then timer handling runs; monotonic
 * time (or intentional rollover) is the caller's responsibility.
 */
void dashboard_render_step(uint32_t now_ms);
/**
 * Refresh invalidated objects and write the resulting RGB image as a binary PPM file.
 *
 * Input: ppm_path is an ordinary output path. Returns false before refresh for a missing
 * display/null path, or on open/header/row/close failure. On success produces P6 header
 * plus 480*272*3 RGB bytes, using lv_color_to32 from the configured RGB565 colors. It does
 * not reinterpret CAN values or synthesize browser graphics.
 */
bool dashboard_render_capture(const char *ppm_path);
/**
 * Delete the generated screens and memory display; perform no hardware operations.
 *
 * Inputs: none. Returns nothing; null display is a no-op. Generated main/debug deletion,
 * separate initial-actions deletion and display removal precede clearing the registered
 * display handle. Shared global LVGL memory/core initialization is retained; no board
 * peripherals are touched.
 */
void dashboard_render_shutdown(void);
/** Press or release the simulated touch panel at display pixel x, y. */
void dashboard_render_touch(int32_t x, int32_t y, bool pressed);

#endif /* DASHBOARD_RENDER_H */
