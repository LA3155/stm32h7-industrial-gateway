#ifndef __TASKS_INIT_H__
#define __TASKS_INIT_H__

#include "main.h"
#include <stdint.h>
#include "cmsis_os2.h"
// 上位机异步下发的写指令载荷定义（全工程唯一声明处）
typedef struct {
    uint8_t  bus_type;        // 1: FDCAN, 2: RS-485
    uint8_t  func_code;       // 0x06 或 0x10
    uint16_t reg_addr;        // 起始地址
    uint16_t reg_count;       // 寄存器数量 (0x06 时为 1)
    uint16_t reg_values[16];  // 数组缓冲区 (单次最多支持连续写入 16 个寄存器)
} fieldbus_write_cmd_t;

// 供全局任务跨线程通信的写指令队列句柄
extern osMessageQueueId_t qFieldbusWriteHandle;

void task_init();

#endif /* __TASKS_INIT_H__ */