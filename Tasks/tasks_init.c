#include "tasks_init.h"
#include "protocol_router.h"
#include "modbus_tcp_server.h"
#include "rs485.h"
#include "can.h"
#include "pvd.h"
#include "tcpip.h"

osMessageQueueId_t qFieldbusWriteHandle;

// 业务任务入口函数声明
extern void task_network_entry(void *argument);
extern void task_fieldbus_entry(void *argument);
extern void task_supervisor_entry(void *argument);

const osThreadAttr_t net_attr = { 
    .name = "NetTask", 
    .stack_size = 1024 * 4, 
    .priority = osPriorityHigh 
};

const osThreadAttr_t bus_attr = { 
    .name = "BusTask", 
    .stack_size = 512 * 4,  
    .priority = osPriorityNormal 
};

const osThreadAttr_t spv_attr = { 
    .name = "SpvTask", 
    .stack_size = 256 * 4,  
    .priority = osPriorityLow 
};

void task_init()
{
    qFieldbusWriteHandle    = osMessageQueueNew(8, sizeof(fieldbus_write_cmd_t), NULL);// 创建全系统通信消息队列,容纳 8 条写指令
    // 初始化共享镜像池与硬件外设
    gateway_regs_init();
    rs485_init();
    can_init();
    pvd_init();
    tcpip_callback((tcpip_callback_fn)modbus_tcp_server_init, NULL);

    osThreadNew(task_network_entry, NULL, &net_attr);
    osThreadNew(task_fieldbus_entry, NULL, &bus_attr);
    osThreadNew(task_supervisor_entry, NULL, &spv_attr);
}