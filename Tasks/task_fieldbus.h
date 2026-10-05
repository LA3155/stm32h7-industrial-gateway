#ifndef __TASK_FIELDBUS_H__
#define __TASK_FIELDBUS_H__

#include <stdint.h>
#include <stdbool.h>

// RS485 从机轮询点位配置
typedef struct {
    uint8_t  slave_id;            // 从机站号
    uint8_t  func_code;           // Modbus 功能码 0x03 或 0x04
    uint16_t start_reg;           // 从机寄存器起始地址
    uint16_t reg_count;           // 读取寄存器数量
    uint16_t gateway_base_reg;    // 映射至网关本地寄存器起始偏移
    uint32_t poll_interval_ms;    // 采集周期
    uint32_t last_poll_tick;      // 上次采集时间戳
    uint16_t timeout_ms;          // 应答超时阈值
    uint8_t  retry_limit;         // 超时重试次数上限
    
    // 运行期诊断统计
    uint8_t  retry_count;         // 当前连续重试计数
    bool     is_online;           // 节点在线状态
    uint32_t total_errors;        // 通信错误累计
} rs485_poll_item_t;

void task_fieldbus_entry(void *argument);
bool task_fieldbus_post_rs485_rx(const uint8_t *data, uint16_t len);

#endif /* __TASK_FIELDBUS_H__ */