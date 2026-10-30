/* Purpose: provide only the STM32 HAL types/constants needed to run production CAN code on a host.
 * CAN frame/DLC constants match the checked-in STM32H7 HAL header; this is not a hardware emulator.
 */
#ifndef CAN_HOST_STM32H7XX_HAL_H
#define CAN_HOST_STM32H7XX_HAL_H
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct {
  uint32_t unused;
} GPIO_TypeDef;
typedef struct {
  uint32_t unused;
} FDCAN_GlobalTypeDef;
extern GPIO_TypeDef fake_gpio_ports[9];
extern FDCAN_GlobalTypeDef fake_fdcan_instance;
#define GPIOA (&fake_gpio_ports[0])
#define GPIOB (&fake_gpio_ports[1])
#define GPIOC (&fake_gpio_ports[2])
#define GPIOD (&fake_gpio_ports[3])
#define GPIOE (&fake_gpio_ports[4])
#define GPIOF (&fake_gpio_ports[5])
#define GPIOG (&fake_gpio_ports[6])
#define GPIOH (&fake_gpio_ports[7])
#define GPIOI (&fake_gpio_ports[8])
#define FDCAN1 (&fake_fdcan_instance)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_AF9_FDCAN1 9U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define ENABLE 1U
#define DISABLE 0U
#define RCC_PERIPHCLK_FDCAN 0x00008000U
#define RCC_FDCANCLKSOURCE_HSE 0x00000000U
#define FDCAN_FRAME_CLASSIC 0U
#define FDCAN_MODE_NORMAL 0U
#define FDCAN_DATA_BYTES_8 0x00000004U
#define FDCAN_TX_FIFO_OPERATION 0U
#define FDCAN_STANDARD_ID 0x00000000U
#define FDCAN_EXTENDED_ID 0x40000000U
#define FDCAN_DATA_FRAME 0x00000000U
#define FDCAN_REMOTE_FRAME 0x20000000U
#define FDCAN_DLC_BYTES_0 0U
#define FDCAN_DLC_BYTES_1 1U
#define FDCAN_DLC_BYTES_2 2U
#define FDCAN_DLC_BYTES_3 3U
#define FDCAN_DLC_BYTES_4 4U
#define FDCAN_DLC_BYTES_5 5U
#define FDCAN_DLC_BYTES_6 6U
#define FDCAN_DLC_BYTES_7 7U
#define FDCAN_DLC_BYTES_8 8U
#define FDCAN_DLC_BYTES_12 9U
#define FDCAN_DLC_BYTES_16 10U
#define FDCAN_DLC_BYTES_20 11U
#define FDCAN_DLC_BYTES_24 12U
#define FDCAN_DLC_BYTES_32 13U
#define FDCAN_DLC_BYTES_48 14U
#define FDCAN_DLC_BYTES_64 15U
#define FDCAN_ESI_ACTIVE 0U
#define FDCAN_BRS_OFF 0U
#define FDCAN_BRS_ON 0x00100000U
#define FDCAN_CLASSIC_CAN 0U
#define FDCAN_FD_CAN 0x00200000U
#define FDCAN_NO_TX_EVENTS 0U
#define FDCAN_FILTER_MASK 2U
#define FDCAN_FILTER_TO_RXFIFO0 1U
#define FDCAN_REJECT 2U
#define FDCAN_REJECT_REMOTE 1U
#define FDCAN_RX_FIFO0 0x00000040U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOI_CLK_ENABLE() ((void)0)
#define __HAL_RCC_FDCAN_CLK_ENABLE() ((void)0)
#define __HAL_RCC_FDCAN_CLK_DISABLE() ((void)0)

typedef struct {
  uint32_t FrameFormat, Mode, AutoRetransmission, TransmitPause, ProtocolException;
  uint32_t NominalPrescaler, NominalSyncJumpWidth, NominalTimeSeg1, NominalTimeSeg2;
  uint32_t DataPrescaler, DataSyncJumpWidth, DataTimeSeg1, DataTimeSeg2;
  uint32_t MessageRAMOffset, StdFiltersNbr, ExtFiltersNbr;
  uint32_t RxFifo0ElmtsNbr, RxFifo0ElmtSize, RxFifo1ElmtsNbr, RxFifo1ElmtSize;
  uint32_t RxBuffersNbr, RxBufferSize, TxEventsNbr, TxBuffersNbr;
  uint32_t TxFifoQueueElmtsNbr, TxFifoQueueMode, TxElmtSize;
} FDCAN_InitTypeDef;
typedef struct {
  FDCAN_GlobalTypeDef *Instance;
  FDCAN_InitTypeDef Init;
  uint32_t ErrorCode;
} FDCAN_HandleTypeDef;
typedef struct {
  uint32_t Identifier, IdType, RxFrameType, DataLength, ErrorStateIndicator;
  uint32_t BitRateSwitch, FDFormat, RxTimestamp, FilterIndex, IsFilterMatchingFrame;
} FDCAN_RxHeaderTypeDef;
typedef struct {
  uint32_t Identifier, IdType, TxFrameType, DataLength, ErrorStateIndicator;
  uint32_t BitRateSwitch, FDFormat, TxEventFifoControl, MessageMarker;
} FDCAN_TxHeaderTypeDef;
typedef struct {
  uint32_t IdType, FilterIndex, FilterType, FilterConfig, FilterID1, FilterID2;
} FDCAN_FilterTypeDef;
typedef struct {
  uint32_t PeriphClockSelection, FdcanClockSelection;
} RCC_PeriphCLKInitTypeDef;
typedef struct {
  uint32_t Pin, Mode, Pull, Speed, Alternate;
} GPIO_InitTypeDef;

/**
 * Host fake HAL implements deterministic clock, FIFO, transmit and initialization
 * boundaries.
 *
 * Inputs: none. Returns the fake uint32 millisecond counter without mutation; tests move
 * it directly to precise request/timeout/wrap boundaries.
 */
uint32_t HAL_GetTick(void);
/**
 * Input: handle ignored. Returns fake_tx_free without mutation, enabling deterministic no-
 * slot retries.
 */
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *handle);
/**
 * Inputs: valid request header/payload; handle ignored. Returns HAL_ERROR with no capture
 * when fake_tx_error is set, otherwise copies eight bytes/header, increments fake_tx_count
 * and returns HAL_OK.
 */
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *handle,
                                                FDCAN_TxHeaderTypeDef *header,
                                                uint8_t *payload);
/**
 * Inputs: handle/fifo ignored. Returns fake_rx_write-fake_rx_read without consuming
 * entries; deliberately bypasses acceptance filtering so decoder rejection remains
 * observable.
 */
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *handle, uint32_t fifo);
/**
 * Inputs: writable header/payload, ignored handle/fifo. Counts every attempted read;
 * error/empty returns HAL_ERROR without consumption. Success asserts valid DLC code,
 * copies its full byte count (up to 64), advances fake_rx_read and returns HAL_OK,
 * reproducing the HAL buffer-size boundary.
 */
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *handle,
                                         uint32_t fifo,
                                         FDCAN_RxHeaderTypeDef *header,
                                         uint8_t *payload);
/**
 * Input: clock ignored. Returns HAL_ERROR for injected initialization stage 1, otherwise
 * HAL_OK; touches no RCC registers.
 */
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *clock);
/**
 * Input: handle ignored. Returns HAL_ERROR for initialization stage 2, otherwise HAL_OK;
 * no controller state is emulated.
 */
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *handle);
/**
 * Inputs: valid filter and ignored handle. Captures the filter before returning stage-3
 * HAL_ERROR or HAL_OK, allowing exact-ID/mask assertions independently of success.
 */
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *handle, FDCAN_FilterTypeDef *filter);
/**
 * Inputs: four acceptance policy constants and ignored handle. Stores each policy then
 * returns stage-4 HAL_ERROR or HAL_OK; actual FIFO admission remains controlled by
 * enqueue.
 */
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *handle,
                                               uint32_t standard,
                                               uint32_t extended,
                                               uint32_t remote_standard,
                                               uint32_t remote_extended);
/**
 * Input: handle ignored. Returns stage-5 HAL_ERROR or HAL_OK; no hardware transmission is
 * enabled.
 */
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *handle);
/**
 * Inputs: port/gpio ignored. Returns nothing; increments fake_gpio_init_count to prove MSP
 * guards and expected call counts without validating electrical pin behavior.
 */
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio);
/**
 * Inputs: port/pin ignored. Returns nothing; increments fake_gpio_deinit_count to observe
 * MSP teardown calls.
 */
void HAL_GPIO_DeInit(GPIO_TypeDef *port, uint32_t pin);
#endif
