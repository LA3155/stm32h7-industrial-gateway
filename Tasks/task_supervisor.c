#include "tasks_init.h"
#include "main.h"

extern IWDG_HandleTypeDef hiwdg1;
// 任务健康标志位掩码 (Bit0: NetTask, Bit1: BusTask)
uint8_t g_task_health_mask = 0;

void task_supervisor_entry(void *argument)
{
    while (1)
    {
        // 检查前两个核心任务是否均在正常轮转 (掩码全为 1)
        if ((g_task_health_mask & 0x03) == 0x03)
        {
            HAL_IWDG_Refresh(&hiwdg1);

            // 清零掩码，等待下一轮各任务打卡
            g_task_health_mask = 0;
        }
        else
        {
        }

        osDelay(500);
    }
}