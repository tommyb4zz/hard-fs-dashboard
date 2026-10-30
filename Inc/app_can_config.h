/**
 * File purpose: Declare peripheral-instance, GPIO-pin and operating-mode types and the immutable board
 * getter. FD appears in the enum for representation but the application currently
 * accepts only classic mode. These structures configure MCU resources and do not
 * describe vehicle protocols or external CAN electrical characteristics.
 */
#ifndef APP_CAN_CONFIG_H
#define APP_CAN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

/* Mode selection: classic is implemented; the FD enumerator is rejected by app_can_init. */
typedef enum {
  APP_CAN_MODE_CLASSIC = 0,
  APP_CAN_MODE_FD,
} app_can_mode_t;

/* One GPIO peripheral pointer, HAL pin bitmask and alternate-function selection. */
typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  uint32_t alternate_function;
} app_can_pin_config_t;

/* Controller and independent RX/TX GPIO tuples; getter returns read-only static storage. */
typedef struct {
  FDCAN_GlobalTypeDef *instance;
  app_can_pin_config_t rx_pin;
  app_can_pin_config_t tx_pin;
  app_can_mode_t mode;
} app_can_config_t;

/**
 * Return the immutable FDCAN instance, pins and classic-CAN mode used by the application.
 *
 * The returned pointer references static const storage with program lifetime: FDCAN1,
 * GPIOH RX14/TX13, AF9, classic mode. Callers must not modify it; there is no allocation,
 * runtime configuration or failure branch.
 */
const app_can_config_t *app_can_config_get(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAN_CONFIG_H */
