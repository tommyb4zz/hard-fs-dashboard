<<<<<<< HEAD
=======
/**
 * File purpose: Register a full-refresh 480-by-272 RGB565 LVGL display using two BSP-addressed
 * SDRAM framebuffers. Configure panel-control GPIO, LTDC pins/PLL/timing/layer and cache
 * maintenance; each flush switches the LTDC base to the rendered buffer. HAL setup
 * failures are fatal. The retained panel timing profile needs fitted-panel verification
 * and is not run by the host memory renderer.
 */

>>>>>>> yousef-dashboard
#include "lvgl_port_lcd.h"

#include "main.h"
#include "stm32h745i_discovery_lcd.h"
#include "stm32h745i_discovery_sdram.h"
#include "lvgl/lvgl.h"

#include <string.h>

<<<<<<< HEAD
=======
/* Persistent handles/buffers must outlive LVGL registration and subsequent flush calls. */
>>>>>>> yousef-dashboard
static LTDC_HandleTypeDef hltdc;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_disp_t *display;
<<<<<<< HEAD

=======
static lv_color_t *const framebuffer_1 = (lv_color_t *)LCD_LAYER_0_ADDRESS;
static lv_color_t *const framebuffer_2 = (lv_color_t *)LCD_LAYER_1_ADDRESS;

/**
 * Enter the application Error_Handler, which disables interrupts and loops indefinitely.
 *
 * No arguments or return result; centralize the fatal path for display setup and address-
 * switch failures. Error_Handler has no recovery path in this application, so callers do
 * not receive an error status.
 */
>>>>>>> yousef-dashboard
static void lcd_error(void) {
  Error_Handler();
}

<<<<<<< HEAD
static void lcd_panel_power_on(void) {
=======
/**
 * Enable the LCD control GPIO clocks and configure output pins; hold backlight, display
 * enable and reset low and select the display mode.
 *
 * Use board BSP GPIO macros, output push-pull/no-pull/low-speed, before applying the
 * initial control levels. Assumes display-enable and backlight pins share the port used
 * for their combined initialization. No GPIO HAL return status is checked.
 */
static void lcd_panel_prepare(void) {
>>>>>>> yousef-dashboard
  GPIO_InitTypeDef gpio = {0};

  LCD_DISP_EN_GPIO_CLK_ENABLE();
  LCD_BL_CTRL_GPIO_CLK_ENABLE();
<<<<<<< HEAD
=======
  LCD_DISPLAY_MODE_GPIO_CLK_ENABLE();
  LCD_RESET_GPIO_CLK_ENABLE();
>>>>>>> yousef-dashboard

  gpio.Pin = LCD_DISP_EN_Pin | LCD_BL_CTRL_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_DISP_EN_GPIO_Port, &gpio);

<<<<<<< HEAD
  HAL_GPIO_WritePin(LCD_DISP_EN_GPIO_Port, LCD_DISP_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
}

=======
  gpio.Pin = LCD_DISPLAY_MODE_Pin;
  HAL_GPIO_Init(LCD_DISPLAY_MODE_GPIO_Port, &gpio);

  gpio.Pin = LCD_RESET_Pin;
  HAL_GPIO_Init(LCD_RESET_GPIO_Port, &gpio);

  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_DISP_EN_GPIO_Port, LCD_DISP_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LCD_DISPLAY_MODE_GPIO_Port, LCD_DISPLAY_MODE_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
}

/**
 * Assert LCD display enable, wait 20 ms, release reset and wait 120 ms using HAL delays.
 *
 * This ordered sequence controls the configured enable/reset pins, not the backlight.
 * Delays are blocking HAL millisecond waits; their compatibility with the fitted panel
 * remains a board-profile verification item.
 */
static void lcd_panel_power_on(void) {
  HAL_GPIO_WritePin(LCD_DISP_EN_GPIO_Port, LCD_DISP_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(20);

  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(120);
}

/**
 * Assert the LCD backlight GPIO after display setup.
 *
 * Drive the configured backlight control pin high after registration and cleared
 * framebuffer setup. There is no brightness/PWM setting, failure return or later automatic
 * shutdown.
 */
static void lcd_backlight_on(void) {
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);
}

/**
 * Configure PLL3 for LTDC, enable LTDC/GPIO clocks, select the display alternate-function
 * pins and reset LTDC; enter the error handler if the peripheral-clock configuration
 * fails.
 *
 * No arguments; configure PLL3 M=5,N=160,R=83 and LTDC source, then AF14 RGB/control pins
 * on K/I/J/H. HAL peripheral-clock failure enters the fatal path, while GPIO operations
 * have no checked result. Reset/release LTDC after pin configuration; no fitted-panel
 * identity is read.
 */
>>>>>>> yousef-dashboard
static void lcd_ltdc_msp_init(void) {
  GPIO_InitTypeDef gpio = {0};
  RCC_PeriphCLKInitTypeDef periph_clk = {0};

  periph_clk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  periph_clk.PLL3.PLL3M = 5;
  periph_clk.PLL3.PLL3N = 160;
  periph_clk.PLL3.PLL3P = 2;
  periph_clk.PLL3.PLL3Q = 2;
  periph_clk.PLL3.PLL3R = 83;
  periph_clk.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
  periph_clk.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  periph_clk.PLL3.PLL3FRACN = 0;

  if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
    lcd_error();
  }

  __HAL_RCC_LTDC_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
<<<<<<< HEAD
  LCD_DISPLAY_MODE_GPIO_CLK_ENABLE();
  LCD_RESET_GPIO_CLK_ENABLE();
=======
>>>>>>> yousef-dashboard

  gpio.Pin = GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_3 | GPIO_PIN_7 | GPIO_PIN_2;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF14_LTDC;
  HAL_GPIO_Init(GPIOK, &gpio);

  gpio.Pin = GPIO_PIN_1 | GPIO_PIN_0 | GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOI, &gpio);

  gpio.Pin = GPIO_PIN_15 | GPIO_PIN_14 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_11 | GPIO_PIN_10 |
             GPIO_PIN_9 | GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_1 |
             GPIO_PIN_5 | GPIO_PIN_3 | GPIO_PIN_4;
  HAL_GPIO_Init(GPIOJ, &gpio);

  gpio.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOH, &gpio);

  __HAL_RCC_LTDC_FORCE_RESET();
  __HAL_RCC_LTDC_RELEASE_RESET();
<<<<<<< HEAD

  gpio.Pin = LCD_DISPLAY_MODE_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LCD_DISPLAY_MODE_GPIO_Port, &gpio);
  HAL_GPIO_WritePin(LCD_DISPLAY_MODE_GPIO_Port, LCD_DISPLAY_MODE_Pin, GPIO_PIN_SET);

  gpio.Pin = LCD_RESET_Pin;
  HAL_GPIO_Init(LCD_RESET_GPIO_Port, &gpio);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
}

=======
}

/**
 * Initialize LTDC timing and its RGB565 layer at the first framebuffer address; enter the
 * error handler on HAL initialization or layer configuration failure.
 *
 * Use BSP RK043FN48H constants, active-low sync/DE and input pixel-clock polarity;
 * configure one opaque RGB565 layer at LCD_LAYER_0_ADDRESS sized LCD_DEFAULT_WIDTH by
 * LCD_DEFAULT_HEIGHT. The horizontal HBP-minus-11 adjustment is retained: active timing
 * calculates 491 clocks while the image layer is 480 pixels. This requires exact-panel
 * evidence before alteration and is not exercised by host rendering.
 */
>>>>>>> yousef-dashboard
static void lcd_ltdc_init(void) {
  LTDC_LayerCfgTypeDef layer = {0};

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = RK043FN48H_HSYNC - 1U;
  hltdc.Init.AccumulatedHBP = RK043FN48H_HSYNC + (RK043FN48H_HBP - 11U) - 1U;
  hltdc.Init.AccumulatedActiveW = RK043FN48H_HSYNC + LCD_DEFAULT_WIDTH + RK043FN48H_HBP - 1U;
  hltdc.Init.TotalWidth =
      RK043FN48H_HSYNC + LCD_DEFAULT_WIDTH + (RK043FN48H_HBP - 11U) + RK043FN48H_HFP - 1U;
  hltdc.Init.VerticalSync = RK043FN48H_VSYNC - 1U;
  hltdc.Init.AccumulatedVBP = RK043FN48H_VSYNC + RK043FN48H_VBP - 1U;
  hltdc.Init.AccumulatedActiveH = RK043FN48H_VSYNC + LCD_DEFAULT_HEIGHT + RK043FN48H_VBP - 1U;
  hltdc.Init.TotalHeigh =
      RK043FN48H_VSYNC + LCD_DEFAULT_HEIGHT + RK043FN48H_VBP + RK043FN48H_VFP - 1U;
  hltdc.Init.Backcolor.Blue = 0x00;
  hltdc.Init.Backcolor.Green = 0x00;
  hltdc.Init.Backcolor.Red = 0x00;

  if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
    lcd_error();
  }

  layer.WindowX0 = 0;
  layer.WindowX1 = LCD_DEFAULT_WIDTH;
  layer.WindowY0 = 0;
  layer.WindowY1 = LCD_DEFAULT_HEIGHT;
  layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  layer.Alpha = 255;
  layer.Alpha0 = 0;
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  layer.FBStartAdress = LCD_LAYER_0_ADDRESS;
  layer.ImageWidth = LCD_DEFAULT_WIDTH;
  layer.ImageHeight = LCD_DEFAULT_HEIGHT;
  layer.Backcolor.Blue = 0;
  layer.Backcolor.Green = 0;
  layer.Backcolor.Red = 0;

  if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) != HAL_OK) {
    lcd_error();
  }
}

<<<<<<< HEAD
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  LV_UNUSED(area);
  LV_UNUSED(color_p);

  SCB_CleanInvalidateDCache();
  lv_disp_flush_ready(drv);
}

=======
/**
 * LVGL flush callback: ignore area, clean/invalidate the data cache, point LTDC layer zero
 * at color_p and signal completion through drv; enter the error handler if changing the
 * address fails.
 *
 * LVGL supplies drv and the full-frame draw buffer color_p. Because full_refresh is
 * enabled, area is intentionally unused and no pixel copy is made: flush all data cache
 * then switch the LTDC layer base. Signal lv_disp_flush_ready immediately after HAL
 * success; no explicit vertical-blank synchronization or controller-completion wait is
 * present here.
 */
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  LV_UNUSED(area);

  SCB_CleanInvalidateDCache();

  if (HAL_LTDC_SetAddress(&hltdc, (uint32_t)color_p, 0) != HAL_OK) {
    lcd_error();
  }

  lv_disp_flush_ready(drv);
}

/**
 * Initialize SDRAM, LCD panel and LTDC, clear two full-screen framebuffers and register a
 * full-refresh LVGL display before enabling backlight; duplicate initialization or failed
 * hardware/display setup enters the error handler.
 *
 * Must follow lv_init and precede ui_init. Initialize BSP SDRAM instance zero, prepare
 * controls/clocks/reset/timing, then zero two width*height buffers, maintain cache and
 * register both as a full-screen LVGL draw buffer. display is a persistent registration
 * pointer; a second call is fatal. Backlight is enabled only after nonnull registration.
 * The host simulator uses a different memory display and does not run this routine.
 */
>>>>>>> yousef-dashboard
void LCD_init(void) {
  if (display != NULL) {
    lcd_error();
  }

  if (BSP_SDRAM_Init(0) != BSP_ERROR_NONE) {
    lcd_error();
  }

<<<<<<< HEAD
  lcd_ltdc_msp_init();
  lcd_ltdc_init();
  lcd_panel_power_on();

  memset(
      (void *)LCD_LAYER_0_ADDRESS, 0, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(lv_color_t));
  SCB_CleanInvalidateDCache();

  lv_disp_draw_buf_init(
      &draw_buf, (void *)LCD_LAYER_0_ADDRESS, NULL, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT);
=======
  lcd_panel_prepare();
  lcd_ltdc_msp_init();
  lcd_panel_power_on();
  lcd_ltdc_init();

  memset(framebuffer_1, 0, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(lv_color_t));
  memset(framebuffer_2, 0, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT * sizeof(lv_color_t));
  SCB_CleanInvalidateDCache();

  lv_disp_draw_buf_init(
      &draw_buf, framebuffer_1, framebuffer_2, LCD_DEFAULT_WIDTH * LCD_DEFAULT_HEIGHT);
>>>>>>> yousef-dashboard

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_DEFAULT_WIDTH;
  disp_drv.ver_res = LCD_DEFAULT_HEIGHT;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
<<<<<<< HEAD
  disp_drv.direct_mode = 1;
=======
  disp_drv.full_refresh = 1;
>>>>>>> yousef-dashboard
  display = lv_disp_drv_register(&disp_drv);

  if (display == NULL) {
    lcd_error();
  }
<<<<<<< HEAD
=======

  lcd_backlight_on();
>>>>>>> yousef-dashboard
}
