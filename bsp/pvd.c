#include "pvd.h"

void pvd_init(void)
{
    PWR_PVDTypeDef sPVDConfig = {0};

    // 开启电源控制时钟与备份域访问
    __HAL_RCC_RTC_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    // 设定2.85V掉电跌落阈值，下降沿中断
    sPVDConfig.PVDLevel = PWR_PVDLEVEL_6;
    sPVDConfig.Mode     = PWR_PVD_MODE_IT_FALLING;
    HAL_PWR_ConfigPVD(&sPVDConfig);
    //使能PVD检测电路
    HAL_PWR_EnablePVD();
    //配置EXTI 16抢占优先级最高
    HAL_NVIC_SetPriority(PVD_AVD_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(PVD_AVD_IRQn);
}

void HAL_PWR_PVDCallback(void)
{
    RTC->BKP0R = 0xDEADBEEF;
    RTC->BKP1R = HAL_GetTick();

    // while(1){

    // }
}