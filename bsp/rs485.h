#ifndef __RS485_H__
#define __RS485_H__

#include "main.h"

#define RS485_DIR_TX()  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port,RS485_DIR_Pin,GPIO_PIN_SET)
#define RS485_DIR_RX()  HAL_GPIO_WritePin(RS485_DIR_GPIO_Port,RS485_DIR_Pin,GPIO_PIN_RESET)

void rs485_init(void);
HAL_StatusTypeDef rs485_send(const uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef rs485_receive(uint8_t *buf, uint16_t max_len, uint16_t *actual_len, uint32_t timeout);

#endif /* __RS485_H */