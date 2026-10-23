/* Purpose: provide a memory UART sink for production binary export, without opening a device. */
#ifndef DASHBOARD_HOST_DISCOVERY_H
#define DASHBOARD_HOST_DISCOVERY_H
#include "stm32h7xx_hal.h"
typedef struct {
  uint32_t unused;
} UART_HandleTypeDef;
extern UART_HandleTypeDef hcom_uart[1];
#define COM1 0U
#define BSP_ERROR_NONE 0
#define HAL_MAX_DELAY UINT32_MAX
/**
 * Append exported bytes to the host's bounded memory sink.
 *
 * Inputs: bytes contains size bytes; uart and timeout are ignored. Returns HAL_ERROR when
 * the 65536-byte memory sink lacks room, otherwise appends bytes, advances wire_size and
 * returns HAL_OK. No serial device, wall-clock wait or partial write occurs.
 */
HAL_StatusTypeDef
HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *bytes, uint16_t size, uint32_t timeout);
#endif
