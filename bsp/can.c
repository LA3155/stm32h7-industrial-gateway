#include "can.h"
#include "protocol_router.h"

extern FDCAN_HandleTypeDef hfdcan1;

HAL_StatusTypeDef can_init(void)
{
    FDCAN_FilterTypeDef sFilterConfig;
    // 1. 配置硬件验收过滤器 (Filter Bank 0)：接收 ID 范围在 0x200 ~ 0x2FF 的标准帧
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_RANGE;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0; // 匹配后存入 Rx FIFO0
    sFilterConfig.FilterID1 = 0x200;                     // 起始 ID
    sFilterConfig.FilterID2 = 0x2FF;                     // 截止 ID

    if(HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig) != HAL_OK){
        return HAL_ERROR;
    }
    // 2. 全局过滤器规则：未命中的非匹配帧一律由硬件直接丢弃 (REJECT)
    if(HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT, 
                                    FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
    {
        return HAL_ERROR;
    }
    // 3. 使能 Rx FIFO 0 新报文到达中断
    if(HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK){
        return HAL_ERROR;
    }
    // 4. 启动 FDCAN 控制器（脱离 INIT 模式进入运行模式）
    if(HAL_FDCAN_Start(&hfdcan1) != HAL_OK){
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef can_send(uint32_t id, const uint8_t *data, uint8_t len)
{
    FDCAN_TxHeaderTypeDef TxHeader;
    TxHeader.Identifier = id;
    TxHeader.IdType = FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.DataLength = (len <= 8) ? len : FDCAN_DLC_BYTES_8;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;       // 传统 CAN 2.0B 模式
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;        // 经典 CAN 帧
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;
    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, (uint8_t *)data);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0){
        FDCAN_RxHeaderTypeDef RxHeader;
        uint8_t RxData[8];

        if(HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK){
            uint16_t target_reg = 100 + (RxHeader.Identifier - 0x200);
            if(target_reg < GATEWAY_REG_TOTAL_NUM){
                g_gateway_regs[target_reg] = (RxData[2] << 8) | RxData[3];
            }
        }
    }
}