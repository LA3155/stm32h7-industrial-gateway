#include "rs485.h"
#include "modbus_crc.h"
#include "lwip/tcp.h"
#include "modbus_tcp_server.h"
#include "protocol_router.h"
#include "task_fieldbus.h"

rs485_rx_packet_t rs485;
extern UART_HandleTypeDef hlpuart1;
#define RS485_RX_BUF_SIZE 256
__attribute__((section(".ram_d3"), aligned(32)))
static uint8_t s_rs485_rx_buf[RS485_RX_BUF_SIZE];

void rs485_start_rx_dma(void)
{
    HAL_UARTEx_ReceiveToIdle_DMA(&hlpuart1, s_rs485_rx_buf, sizeof(s_rs485_rx_buf));
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if(huart->Instance == LPUART1){
        if (Size > 0 && Size <= RS485_MAX_FRAME_LEN) {
            task_fieldbus_post_rs485_rx(s_rs485_rx_buf, Size);
        }
        rs485_start_rx_dma();
    }
}

void rs485_init(void)
{
    RS485_DIR_RX();
    rs485_start_rx_dma();
}

HAL_StatusTypeDef rs485_send(const uint8_t *data, uint16_t len, uint32_t timeout)
{
    if(data == NULL || len == 0){
        return HAL_ERROR;
    }

    RS485_DIR_TX();

    __HAL_UART_CLEAR_FLAG(&hlpuart1, UART_CLEAR_TCF);

    HAL_StatusTypeDef status = HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, len, timeout);

    uint32_t tickstart = HAL_GetTick();
    while(__HAL_UART_GET_FLAG(&hlpuart1, UART_FLAG_TC) == RESET){
        if(HAL_GetTick() - tickstart > timeout){
            status = HAL_TIMEOUT;
            break;
        }
    }

    RS485_DIR_RX();

    return status;
}
