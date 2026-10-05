#include "task_fieldbus.h"
#include "tasks_init.h"
#include "protocol_router.h"
#include "rs485.h"
#include "can.h"
#include "modbus_crc.h"
#include "main.h"
#include "stdbool.h"

extern uint8_t g_task_health_mask;
static osMessageQueueId_t s_q_rs485_rx = NULL;
uint32_t last_poll_tick;

// 采集点位表配置
static rs485_poll_item_t s_poll_table[] = {
    {
        .slave_id = 0x01,
        .func_code = 0x03,
        .start_reg = 0x0000,
        .reg_count = 5,
        .gateway_base_reg = 200,
        .poll_interval_ms = 10000,
        .timeout_ms = 300,
        .retry_limit = 2,
        .is_online = true
    }
};

#define POLL_TABLE_SIZE (sizeof(s_poll_table) / sizeof(s_poll_table[0]))

typedef enum {
    BUS_STATE_IDLE = 0,
    BUS_STATE_WAIT_RESPONSE
} bus_master_state_t;

typedef enum {
    TX_TYPE_NONE = 0,
    TX_TYPE_POLL_READ,
    TX_TYPE_ASYNC_WRITE
} bus_tx_type_t;

// 总线运行期动态状态
typedef struct {
    bus_master_state_t state;
    bus_tx_type_t      tx_type;
    uint32_t           send_tick;
    uint8_t            poll_index;
} bus_transaction_context_t;

static bus_transaction_context_t s_trans_ctx;

void task_fieldbus_init_queues(void)
{
    if (s_q_rs485_rx == NULL) {
        s_q_rs485_rx = osMessageQueueNew(4, sizeof(rs485_rx_packet_t), NULL);
    }
}
bool task_fieldbus_post_rs485_rx(const uint8_t *data, uint16_t len)
{
    if (s_q_rs485_rx == NULL || data == NULL || len == 0 || len > RS485_MAX_FRAME_LEN) {
        return false;
    }
    rs485_rx_packet_t pkt;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    return (osMessageQueuePut(s_q_rs485_rx, &pkt, 0, 0) == osOK);
}

// 组装并发送 Modbus-RTU 读保持/输入寄存器帧
static void send_modbus_read_req(uint8_t slave_id, uint8_t fc, uint16_t start_reg, uint16_t count)
{
    uint8_t frame[8];
    frame[0] = slave_id;
    frame[1] = fc;
    frame[2] = (uint8_t)(start_reg >> 8);
    frame[3] = (uint8_t)(start_reg & 0xFF);
    frame[4] = (uint8_t)(count >> 8);
    frame[5] = (uint8_t)(count & 0xFF);
    uint16_t crc = modbus_crc16_calculate(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    rs485_send(frame, sizeof(frame), 100);
}

static void fieldbus_fsm_handle_idle(uint32_t now)
{
    fieldbus_write_cmd_t write_cmd;

    // 写指令抢占调度
    if (osMessageQueueGet(qFieldbusWriteHandle, &write_cmd, NULL, 0) == osOK) {
        if (write_cmd.bus_type == 1) {
            for (uint16_t i = 0; i < write_cmd.reg_count; i++) {
                uint8_t payload[2] = {
                    (uint8_t)(write_cmd.reg_values[i] >> 8),
                    (uint8_t)(write_cmd.reg_values[i] & 0xFF)
                };
                can_send(0x200 + (write_cmd.reg_addr + i - 100), payload, 2);
            }
        } else if (write_cmd.bus_type == 2) {
            uint16_t dev_reg = write_cmd.reg_addr - 200;
            if (write_cmd.func_code == 0x06) {
                uint8_t rtu_frame[8] = {
                    0x01, 0x06,
                    (uint8_t)(dev_reg >> 8), (uint8_t)(dev_reg & 0xFF),
                    (uint8_t)(write_cmd.reg_values[0] >> 8), (uint8_t)(write_cmd.reg_values[0] & 0xFF)
                };
                uint16_t crc = modbus_crc16_calculate(rtu_frame, 6);
                rtu_frame[6] = (uint8_t)(crc & 0xFF);
                rtu_frame[7] = (uint8_t)(crc >> 8);

                s_trans_ctx.state = BUS_STATE_WAIT_RESPONSE;
                s_trans_ctx.tx_type = TX_TYPE_ASYNC_WRITE;
                s_trans_ctx.send_tick = now;
                rs485_send(rtu_frame, sizeof(rtu_frame), 100);
            }
        }
        return;
    }

    // 周期轮询点位表
    for (uint8_t i = 0; i < POLL_TABLE_SIZE; i++) {
        rs485_poll_item_t *item = &s_poll_table[i];
        if (now - item->last_poll_tick >= item->poll_interval_ms) {
            item->last_poll_tick = now;

            s_trans_ctx.state = BUS_STATE_WAIT_RESPONSE;
            s_trans_ctx.tx_type = TX_TYPE_POLL_READ;
            s_trans_ctx.poll_index = i;
            s_trans_ctx.send_tick = now;

            send_modbus_read_req(item->slave_id, item->func_code, item->start_reg, item->reg_count);
            break;
        }
    }
}

static void fieldbus_fsm_handle_wait(uint32_t now)
{
    rs485_rx_packet_t rx_pkt;
    rs485_poll_item_t *item = &s_poll_table[s_trans_ctx.poll_index];

    // 处理回包
    if (osMessageQueueGet(s_q_rs485_rx, &rx_pkt, NULL, 0) == osOK) {
        if (rx_pkt.len >= 5) {
            uint16_t cal_crc = modbus_crc16_calculate(rx_pkt.data, rx_pkt.len - 2);
            uint16_t recv_crc = (rx_pkt.data[rx_pkt.len - 1] << 8) | rx_pkt.data[rx_pkt.len - 2];

            if (cal_crc == recv_crc) {
                if (s_trans_ctx.tx_type == TX_TYPE_POLL_READ) {
                    // 校验回包站号与功能码是否匹配当前点位配置
                    if (rx_pkt.data[0] == item->slave_id && rx_pkt.data[1] == item->func_code) {
                        uint8_t byte_count = rx_pkt.data[2];
                        uint8_t actual_regs = byte_count / 2;

                        if (rx_pkt.len == (uint16_t)(3 + byte_count + 2)) {
                            for (uint8_t r = 0; r < actual_regs && r < item->reg_count; r++) {
                                uint16_t reg_idx = item->gateway_base_reg + r;
                                if (reg_idx < GATEWAY_REG_TOTAL_NUM) {
                                    g_gateway_regs[reg_idx] = (rx_pkt.data[3 + r * 2] << 8) |
                                                              (rx_pkt.data[3 + r * 2 + 1]);
                                }
                            }
                            item->is_online = true;
                            item->retry_count = 0;
                            s_trans_ctx.state = BUS_STATE_IDLE;
                            return;
                        }
                    }
                } else if (s_trans_ctx.tx_type == TX_TYPE_ASYNC_WRITE) {
                    s_trans_ctx.state = BUS_STATE_IDLE;
                    return;
                }
            }
        }
    }

    // 超时检测
    if (s_trans_ctx.tx_type == TX_TYPE_POLL_READ) {
        if (now - s_trans_ctx.send_tick >= item->timeout_ms) {
            item->retry_count++;
            item->total_errors++;

            if (item->retry_count > item->retry_limit) {
                item->is_online = false;
                item->retry_count = 0;
                s_trans_ctx.state = BUS_STATE_IDLE;
            } else {
                s_trans_ctx.send_tick = now;
                send_modbus_read_req(item->slave_id, item->func_code, item->start_reg, item->reg_count);
            }
        }
    } else {
        if (now - s_trans_ctx.send_tick >= 300) {
            s_trans_ctx.state = BUS_STATE_IDLE;
        }
    }
}

void task_fieldbus_entry(void *argument)
{
    task_fieldbus_init_queues();
    memset(&s_trans_ctx, 0, sizeof(s_trans_ctx));

    while (1) {
        uint32_t now = HAL_GetTick();

        if (s_trans_ctx.state == BUS_STATE_IDLE) {
            fieldbus_fsm_handle_idle(now);
        } else if (s_trans_ctx.state == BUS_STATE_WAIT_RESPONSE) {
            fieldbus_fsm_handle_wait(now);
        }

        g_task_health_mask |= (1 << 1);
        osDelay(10);
    }
}