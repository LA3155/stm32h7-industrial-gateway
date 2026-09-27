#include "tasks_init.h"
#include "protocol_router.h"
#include "rs485.h"
#include "can.h"
#include "modbus_crc.h"

extern uint8_t g_task_health_mask;

// 1 秒定时周期轮询触发标志位
static volatile uint8_t s_rs485_poll_trigger = 0;

// FreeRTOS 软件定时器回调函数 (由 Tmr Svc 任务以 1000ms 周期执行)
void timer_poll_callback(void *argument)
{
    // 仅置位标志，耗时 < 1 微秒，避免阻塞 FreeRTOS 定时器守护线程
    s_rs485_poll_trigger = 1;
}

void task_fieldbus_entry(void *argument)
{
    fieldbus_write_cmd_t write_cmd;

    while(1){
        if(osMessageQueueGet(qFieldbusWriteHandle, &write_cmd, NULL, 0) == osOK){
            if(write_cmd.bus_type == 1){
                for(uint16_t i = 0; i < write_cmd.reg_count; i++){
                    uint8_t payload[2];
                    payload[0] = (uint8_t)(write_cmd.reg_values[i] >> 8);
                    payload[1] = (uint8_t)(write_cmd.reg_values[i] & 0xFF);
                    can_send(0x200 + (write_cmd.reg_addr + i - 100), payload, 2);
                }
            }
            else if(write_cmd.bus_type == 2){
                uint16_t dev_reg = write_cmd.reg_addr - 200; // 换算为 485 从机内部相对寄存器偏移地址
                // 写单寄存器 (FC 0x06)
                if (write_cmd.func_code == 0x06) {
                    uint8_t rtu_frame[8];
                    rtu_frame[0] = 0x01;                                  // 从机站号 Slave ID (单机测试固定为 1)
                    rtu_frame[1] = 0x06;                                  // 功能码 0x06 (写单个保持寄存器)
                    rtu_frame[2] = (uint8_t)(dev_reg >> 8);               // 寄存器起始地址高字节
                    rtu_frame[3] = (uint8_t)(dev_reg & 0xFF);             // 寄存器起始地址低字节
                    rtu_frame[4] = (uint8_t)(write_cmd.reg_values[0] >> 8);   // 写入数值高字节
                    rtu_frame[5] = (uint8_t)(write_cmd.reg_values[0] & 0xFF); // 写入数值低字节
                    uint16_t crc = modbus_crc16_calculate(rtu_frame, 6);
                    rtu_frame[6] = (uint8_t)(crc & 0xFF);                 // CRC 低字节优先
                    rtu_frame[7] = (uint8_t)(crc >> 8);                   // CRC 高字节在后
                    rs485_send(rtu_frame, sizeof(rtu_frame), 100);
                }
                // B. 写多寄存器 (FC 0x10)
                else if (write_cmd.func_code == 0x10) {
                    uint8_t rtu_frame[64];
                    uint8_t byte_count = (uint8_t)(write_cmd.reg_count * 2);
                    rtu_frame[0] = 0x01; // 从机站号
                    rtu_frame[1] = 0x10; // 功能码 0x10
                    rtu_frame[2] = (uint8_t)(dev_reg >> 8);
                    rtu_frame[3] = (uint8_t)(dev_reg & 0xFF);
                    rtu_frame[4] = (uint8_t)(write_cmd.reg_count >> 8);
                    rtu_frame[5] = (uint8_t)(write_cmd.reg_count & 0xFF);
                    rtu_frame[6] = byte_count;
                    for (uint16_t i = 0; i < write_cmd.reg_count; i++) {
                        rtu_frame[7 + i * 2]     = (uint8_t)(write_cmd.reg_values[i] >> 8);
                        rtu_frame[7 + i * 2 + 1] = (uint8_t)(write_cmd.reg_values[i] & 0xFF);
                    }
                    uint16_t frame_len = 7 + byte_count;
                    uint16_t crc = modbus_crc16_calculate(rtu_frame, frame_len);
                    rtu_frame[frame_len]     = (uint8_t)(crc & 0xFF);
                    rtu_frame[frame_len + 1] = (uint8_t)(crc >> 8);
                    rs485_send(rtu_frame, frame_len + 2, 100);
                }
            }
        }

        // 检查是否有 1 秒定时从机数据采集触发 (Master Poll)
        if (s_rs485_poll_trigger) {
            s_rs485_poll_trigger = 0; // 清除触发标志

            // 向 1 号从机发送读保持寄存器指令: 读寄存器 0 开始的 2 个数据 (温度/湿度)
            // 报文结构: [01(站号)] [03(FC)] [00 00(起始地址)] [00 02(数量)] [C4 0B(CRC16)]
            uint8_t poll_frame[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
            rs485_send(poll_frame, sizeof(poll_frame), 100);
        }

        g_task_health_mask |= (1 << 1);
        osDelay(100);
    }
}