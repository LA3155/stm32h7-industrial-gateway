#include "tasks_init.h"
#include "protocol_router.h"

extern uint8_t g_task_health_mask;

void task_network_entry(void *argument)
{
    while(1){
        g_task_health_mask |= (1 << 0);
        osDelay(100);
    }
}