/**
 * File purpose: Provide the immutable board CAN selection consumed by initialization and HAL MSP
 * pin setup. Select FDCAN1 with PH14 RX and PH13 TX at AF9 in classic mode, matching the supplied
 * MB1381 design routing. This mapping defines MCU peripheral pins only, not harness wiring,
 * termination or vehicle message requirements.
 */
#include "app_can_config.h"

static const app_can_config_t app_can_config = {
    .instance = FDCAN1,
    /*
     * FDCAN routing:
     *   D16 -> PH13 -> FDCAN1_TX
     *   B17 -> PH14 -> FDCAN1_RX
     */
    .rx_pin =
        {
            .port = GPIOH,
            .pin = GPIO_PIN_14,
            .alternate_function = GPIO_AF9_FDCAN1,
        },
    .tx_pin =
        {
            .port = GPIOH,
            .pin = GPIO_PIN_13,
            .alternate_function = GPIO_AF9_FDCAN1,
        },
    .mode = APP_CAN_MODE_CLASSIC,
};

/**
 * Return the immutable FDCAN instance, pins and classic-CAN mode used by the application.
 *
 * The returned pointer references static const storage with program lifetime: FDCAN1,
 * GPIOH RX14/TX13, AF9, classic mode. Callers must not modify it; there is no allocation,
 * runtime configuration or failure branch.
 */
const app_can_config_t *app_can_config_get(void) {
  return &app_can_config;
}
