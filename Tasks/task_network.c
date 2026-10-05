#include "tasks_init.h"
#include "protocol_router.h"
#include "lwip/netif.h"

extern struct netif gnetif;
extern uint8_t g_task_health_mask;

void task_network_entry(void *argument)
{
    while(1){
        // 刷新系统运行时间戳/S
        uint32_t uptime_sec = HAL_GetTick() / 1000;
        g_gateway_regs[0] = (uint16_t)(uptime_sec & 0xFFFF);
        g_gateway_regs[1] = (uint16_t)((uptime_sec >> 16) & 0xFFFF);
        g_gateway_regs[3] = netif_is_link_up(&gnetif) ? 1 : 0;

        g_task_health_mask |= (1 << 0);
        osDelay(100);
    }
}