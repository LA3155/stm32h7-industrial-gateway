#ifndef __CAN_H__
#define __CAN_H__

#include "main.h"

HAL_StatusTypeDef can_init(void);
HAL_StatusTypeDef can_send(uint32_t id, const uint8_t *data, uint8_t len);

#endif /* __CAN_H */