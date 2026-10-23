/* Purpose: Declare the fake UART transport used to capture production binary exports. */
#ifndef STORAGE_TEST_DISCOVERY_H
#define STORAGE_TEST_DISCOVERY_H
#include <stdint.h>
typedef int UART_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
#define COM1 0
#define HAL_MAX_DELAY UINT32_MAX
extern UART_HandleTypeDef hcom_uart[1];
/**
 * Inputs: bytes/count are binary export content; timeout must be HAL_MAX_DELAY, uart is
 * ignored. Returns HAL_ERROR for injected failure; otherwise bounds are asserted, bytes
 * are appended to wire and wire_count advances. It never writes a physical UART.
 */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
#endif
