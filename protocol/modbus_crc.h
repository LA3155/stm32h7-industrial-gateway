#ifndef __MODBUS_CRC_H__
#define __MODBUS_CRC_H__

#include <stdint.h>
#include <stddef.h>

extern const uint16_t s_crc16_table[256];

uint16_t modbus_crc16_calculate(const uint8_t *data,size_t length);
int32_t modbus_tcp_to_rtu_pack(const uint8_t *tcp_frame, size_t tcp_len, 
                               uint8_t *out_rtu_frame, size_t *out_rtu_len);

#endif
