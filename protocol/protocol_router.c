#include "protocol_router.h"
#include "modbus_crc.h"
#include "rs485.h"
#include "can.h"
#include "tasks_init.h"

uint16_t g_gateway_regs[GATEWAY_REG_TOTAL_NUM] = {0};

typedef int32_t (*route_handler_fn)(const uint8_t *frame,size_t len,uint8_t *out_resp,size_t *out_resp_len);
static int32_t handle_modbus_to_rs485(const uint8_t *tcp_frame, size_t tcp_len, 
                                uint8_t *out_resp, size_t *out_resp_len) 
{
    uint8_t func_code = tcp_frame[7];
    fieldbus_write_cmd_t cmd;
    cmd.bus_type = 2;
    
    //写单个寄存器
    if(func_code == 0x06){
        if(tcp_len < 12) return ROUTER_ERR_INVALID_PARAM;

        uint16_t reg_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);
        uint16_t reg_val  = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);

        if(reg_addr >= GATEWAY_REG_TOTAL_NUM) return ROUTER_ERR_INVALID_PARAM;
        //刷新共享池
        g_gateway_regs[reg_addr] = reg_val;
        
        cmd.func_code      = 0x06;
        cmd.reg_addr       = reg_addr;
        cmd.reg_count      = 1;
        cmd.reg_values[0]  = reg_val;

        if (qFieldbusWriteHandle != NULL) {
            osMessageQueuePut(qFieldbusWriteHandle, &cmd, 0, 0);
        }

        memcpy(out_resp, tcp_frame, 12);
        *out_resp_len = 12;
        return ROUTER_OK;
    }
    //写多个寄存器
    else if(func_code == 0x10){
        if (tcp_len < 13) return ROUTER_ERR_INVALID_PARAM;

        uint16_t start_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);
        uint16_t reg_count  = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);
        uint8_t  byte_count = tcp_frame[12];

        if (reg_count == 0 || reg_count > 16) return ROUTER_ERR_INVALID_PARAM;
        if (byte_count != reg_count * 2) return ROUTER_ERR_INVALID_PARAM;
        if (tcp_len < (size_t)(13 + byte_count)) return ROUTER_ERR_INVALID_PARAM;
        if (start_addr + reg_count > GATEWAY_REG_TOTAL_NUM) return ROUTER_ERR_INVALID_PARAM;
        
        cmd.func_code = 0x10;
        cmd.reg_addr  = start_addr;
        cmd.reg_count = reg_count;

        for (uint16_t i = 0; i < reg_count; i++) {
            uint16_t reg_val = (uint16_t)((tcp_frame[13 + i * 2] << 8) | tcp_frame[13 + i * 2 + 1]);
            g_gateway_regs[start_addr + i] = reg_val;
            cmd.reg_values[i] = reg_val;
        }

        if (qFieldbusWriteHandle != NULL) {
            osMessageQueuePut(qFieldbusWriteHandle, &cmd, 0, 0);
        }

        memcpy(out_resp, tcp_frame, 12);
        out_resp[4] = 0x00; // 长度高字节
        out_resp[5] = 0x06; // 长度低字节 (UnitID 1B + FC 1B + Addr 2B + Count 2B = 6B)
        *out_resp_len = 12;
        return ROUTER_OK;
    }
    return ROUTER_ERR_NO_ROUTE;
}

static int32_t handle_modbus_to_can(const uint8_t *tcp_frame, size_t tcp_len, 
                                    uint8_t *out_resp, size_t *out_resp_len)
{
    uint8_t func_code = tcp_frame[7];
    fieldbus_write_cmd_t cmd;
    cmd.bus_type = 1;

    //写单个寄存器
    if (func_code == 0x06) {
        if (tcp_len < 12) return ROUTER_ERR_INVALID_PARAM;
        uint16_t reg_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);
        uint16_t reg_val  = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);
        // 校验是否在 CAN 分区 (100 ~ 199)
        if (reg_addr < 100 || reg_addr >= 200) return ROUTER_ERR_INVALID_PARAM;

        g_gateway_regs[reg_addr] = reg_val;
        // 装载事务结构体
        cmd.func_code     = 0x06;
        cmd.reg_addr      = reg_addr;
        cmd.reg_count     = 1;
        cmd.reg_values[0] = reg_val;
        // 非阻塞投递队列 
        if (qFieldbusWriteHandle != NULL) {
            osMessageQueuePut(qFieldbusWriteHandle, &cmd, 0, 0);
        }
        // 构建 Modbus TCP 标准 FC 0x06 成功应答,原样回显 12 字节
        memcpy(out_resp, tcp_frame, 12);
        *out_resp_len = 12;
        return ROUTER_OK;
    }
    // 处理写多寄存器 (FC 0x10)
    else if (func_code == 0x10) {
        // 确保可以安全读取 byte_count (13 字节)
        if (tcp_len < 13) return ROUTER_ERR_INVALID_PARAM;
        uint16_t start_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);
        uint16_t reg_count  = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);
        uint8_t  byte_count = tcp_frame[12];
        // 寄存器数量边界、数据字节与整包长度 (至少 15 字节)
        if (reg_count == 0 || reg_count > 16) return ROUTER_ERR_INVALID_PARAM;
        if (byte_count != reg_count * 2) return ROUTER_ERR_INVALID_PARAM;
        if (tcp_len < (size_t)(13 + byte_count)) return ROUTER_ERR_INVALID_PARAM;
        if (start_addr < 100 || (start_addr + reg_count) > 200) return ROUTER_ERR_INVALID_PARAM;
        // 批量更新镜像池与装载事务数据
        cmd.func_code = 0x10;
        cmd.reg_addr  = start_addr;
        cmd.reg_count = reg_count;
        for (uint16_t i = 0; i < reg_count; i++) {
            uint16_t reg_val = (uint16_t)((tcp_frame[13 + i * 2] << 8) | tcp_frame[13 + i * 2 + 1]);
            g_gateway_regs[start_addr + i] = reg_val;
            cmd.reg_values[i] = reg_val;
        }
        // 整包事务单次入队 (占用 1 个队列槽位)
        if (qFieldbusWriteHandle != NULL) {
            osMessageQueuePut(qFieldbusWriteHandle, &cmd, 0, 0);
        }
        // 构建 Modbus TCP 标准 FC 0x10 成功应答 (12 字节: MBAP + FC + StartAddr + Count)
        memcpy(out_resp, tcp_frame, 12);
        out_resp[4] = 0x00;
        out_resp[5] = 0x06;
        *out_resp_len = 12;
        return ROUTER_OK;
    }
    
    return ROUTER_ERR_PACK_FAIL;
}
//网关共享数据池
void gateway_regs_init(void)
{
    // 初始化网关基础运行参数
    g_gateway_regs[0] = 0;      // 系统运行时间低 16 位
    g_gateway_regs[1] = 0;      // 系统运行时间高 16 位
    g_gateway_regs[2] = 0x0100; // 固件版本: V1.0.0
    g_gateway_regs[3] = 0;      // 以太网链路状态 (0:Down, 1:Up)
    g_gateway_regs[5] = 10;     // RS-485 默认轮询周期: 10 秒 (上位机可写 06 修改)
    
    // 初始化 CAN 设备默认模拟镜像
    g_gateway_regs[100] = 480;  // 模拟 BMS 电压 48.0V
    g_gateway_regs[101] = 125;  // 模拟电流 12.5A
    
    // 初始化 485 设备默认模拟镜像
    g_gateway_regs[200] = 256;  // 模拟环境温度 25.6℃
    g_gateway_regs[201] = 600;  // 模拟湿度 60.0%
}

static int32_t handle_read_holding_registers(const uint8_t *tcp_frame, size_t tcp_len, 
                                            uint8_t *out_resp, size_t *out_resp_len)
{
    if(tcp_len < 12) return ROUTER_ERR_INVALID_PARAM;

    uint16_t start_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);
    uint16_t reg_count  = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);

    // 边界越界保护 (单次最大允许读取 125 个寄存器)
    if (start_addr + reg_count > GATEWAY_REG_TOTAL_NUM || reg_count == 0 || reg_count > 125) {
        return ROUTER_ERR_INVALID_PARAM;
    }

    // 构建 7 字节 MBAP 报头
    out_resp[0] = tcp_frame[0]; // Transaction ID 高
    out_resp[1] = tcp_frame[1]; // Transaction ID 低
    out_resp[2] = 0x00;         // Protocol ID
    out_resp[3] = 0x00;

    uint16_t byte_count = reg_count * 2;
    uint16_t pdu_len    = 1 + 1 + 1 + byte_count; // UnitID(1B) + FC(1B) + ByteCount(1B) + Data
    out_resp[4] = (uint8_t)(pdu_len >> 8);
    out_resp[5] = (uint8_t)(pdu_len & 0xFF);
    out_resp[6] = tcp_frame[6]; // Unit ID

    // 构建 Modbus PDU 响应数据
    out_resp[7] = 0x03;                    // Function Code
    out_resp[8] = (uint8_t)byte_count;     // 数据字节数

    for(uint16_t i = 0; i< reg_count; i++){
        uint16_t val = g_gateway_regs[start_addr + i];
        out_resp[9 + i * 2]     = (uint8_t)(val >> 8);
        out_resp[9 + i * 2 + 1] = (uint8_t)(val & 0xFF);
    }

    *out_resp_len = 7 + 2 + byte_count; // 7字节MBAP + 1字节FC + 1字节字节数 + 数据长度
    return ROUTER_OK;
}

int32_t protocol_router_dispatch(const uint8_t *tcp_frame, size_t tcp_len, 
                                uint8_t *out_resp, size_t *out_resp_len)
{
    if(tcp_frame == NULL || tcp_len <= 10 || out_resp == NULL || out_resp_len == NULL){
        return ROUTER_ERR_INVALID_PARAM;
    }

    uint8_t func_code = tcp_frame[7];
    //读保持寄存器 (FC 0x03)：统一查镜像表极速秒回
    if(func_code == 0x03){
        return handle_read_holding_registers(tcp_frame, tcp_len, out_resp, out_resp_len);
    }
    // 写单个寄存器 (FC 0x06) 或 写多个寄存器 (FC 0x10)：根据地址分流下发总线
    else if(func_code == 0x06 || func_code == 0x10){
        uint16_t reg_addr = (uint16_t)((tcp_frame[8] << 8) | tcp_frame[9]);

        if (reg_addr < 100 && func_code == 0x06) {
            uint16_t reg_val = (uint16_t)((tcp_frame[10] << 8) | tcp_frame[11]);
            g_gateway_regs[reg_addr] = reg_val; // 直接更新参数
            memcpy(out_resp, tcp_frame, 12);     // 原样回显应答
            *out_resp_len = 12;
            return ROUTER_OK;
        }
        else if (reg_addr >= 100 && reg_addr <= 199) {
            return handle_modbus_to_can(tcp_frame, tcp_len, out_resp, out_resp_len);
        } else if (reg_addr >= 200 && reg_addr <= 299) {
            return handle_modbus_to_rs485(tcp_frame, tcp_len, out_resp, out_resp_len);
        }
    }

    return ROUTER_ERR_NO_ROUTE;
}