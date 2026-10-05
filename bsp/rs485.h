#ifndef __RS485_H__
#define __RS485_H__

#include "main.h"

#define RS485_MAX_FRAME_LEN 64
typedef struct {
    uint16_t len;
    uint8_t  data[RS485_MAX_FRAME_LEN];
} rs485_rx_packet_t;
// 外部声明接收队列句柄

#define RS485_DIR_TX()  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port,RS485_DIR_Pin,GPIO_PIN_SET)
#define RS485_DIR_RX()  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port,RS485_DIR_Pin,GPIO_PIN_RESET)

void rs485_init(void);
HAL_StatusTypeDef rs485_send(const uint8_t *data, uint16_t len, uint32_t timeout);

#endif /* __RS485_H */