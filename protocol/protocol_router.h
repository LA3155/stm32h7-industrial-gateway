#ifndef __PROTOCOL_ROUTER_H__
#define __PROTOCOL_ROUTER_H__

#include <stdint.h>
#include <stddef.h>
#include "string.h"
#define ROUTER_OK                 0
#define ROUTER_ERR_INVALID_PARAM -1
#define ROUTER_ERR_NO_ROUTE      -2
#define ROUTER_ERR_PACK_FAIL     -3

#define GATEWAY_REG_TOTAL_NUM    300


typedef struct{
    uint32_t id;       // CAN 标识符 (如 0x181)
    uint8_t  is_ext;    // 0: 标准 11 位帧, 1: 扩展 29 位帧
    uint8_t  dlc;       // 数据长度代码 (0 ~ 8 字节)
    uint8_t  data[8];   // 数据载荷
}can_frame_t;

extern uint16_t g_gateway_regs[GATEWAY_REG_TOTAL_NUM];

int32_t protocol_router_dispatch(const uint8_t *tcp_frame, size_t tcp_len, 
                                 uint8_t *out_resp, size_t *out_resp_len);
void gateway_regs_init(void);
#endif