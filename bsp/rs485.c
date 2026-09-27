#include "rs485.h"
#include "modbus_crc.h"
#include "lwip/tcp.h"
#include "modbus_tcp_server.h"
#include "protocol_router.h"

extern UART_HandleTypeDef hlpuart1;
#define RS485_RX_BUF_SIZE 256
static uint8_t s_rs485_rx_buf[RS485_RX_BUF_SIZE];

void rs485_start_rx_it(void)
{
    HAL_UARTEx_ReceiveToIdle_IT(&hlpuart1, s_rs485_rx_buf, sizeof(s_rs485_rx_buf));
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if(huart->Instance == LPUART1){
        if(Size >= 5 ){
            uint16_t cal_crc = modbus_crc16_calculate(s_rs485_rx_buf, Size - 2);
            uint16_t recv_crc = (s_rs485_rx_buf[Size - 1] << 8) | s_rs485_rx_buf[Size - 2];

            if(cal_crc == recv_crc){
                // 从机回复读指令 (01 03 02 XX XX CRC CRC)，提取数据刷新到 200 号寄存器
                if (s_rs485_rx_buf[1] == 0x03 && Size >= 7) {
                    g_gateway_regs[200] = (s_rs485_rx_buf[3] << 8) | s_rs485_rx_buf[4];
                }
            }
        }
        rs485_start_rx_it();
    }
}

void rs485_init(void)
{
    RS485_DIR_RX();
    rs485_start_rx_it();
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

HAL_StatusTypeDef rs485_receive(uint8_t *buf, uint16_t max_len, uint16_t *actual_len, uint32_t timeout)
{
    // 初始验证阶段采用带超时的阻塞接收（后续阶段可升级为 DMA + 空闲中断 IDLE）
    HAL_StatusTypeDef status = HAL_UART_Receive(&hlpuart1, buf, max_len, timeout);
    return status;
}