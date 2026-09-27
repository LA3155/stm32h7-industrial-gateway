"""
STM32H753 工业网关协议转换与 CRC16 自动化单元测试框架
用于在 PC 端离线验证协议转换逻辑，并为硬件联调提供自动化回归测试桩
"""

import sys
import time
import random

# 确保 Windows 终端支持 UTF-8 中文与字符输出
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding='utf-8')

# ==============================================================================
# 1. 黄金真值参考模型 (Golden Reference Model)
# ==============================================================================

# 标准 Modbus 0xA001 反转多项式 256 项速查表
CRC16_TABLE = [
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
]

def golden_modbus_crc(data: bytes) -> int:
    """标准 CRC16 黄金真值计算"""
    crc = 0xFFFF
    for byte in data:
        idx = (crc ^ byte) & 0xFF
        crc = ((crc >> 8) ^ CRC16_TABLE[idx]) & 0xFFFF
    return crc

def golden_tcp_to_rtu(tcp_packet: bytes):
    """
    MBAP 帧剥离与 RTU 封包黄金真值转换:
    [MBAP Header 7 字节: TransID(2), ProtoID(2), Length(2), UnitID(1)] + [PDU]
    -> [UnitID] + [PDU] + [CRC_L] + [CRC_H]
    """
    if len(tcp_packet) < 7:
        return None, -1
    
    rtu_pdu = tcp_packet[6:]
    crc = golden_modbus_crc(rtu_pdu)
    crc_low = crc & 0xFF
    crc_high = (crc >> 8) & 0xFF
    
    rtu_frame = rtu_pdu + bytes([crc_low, crc_high])
    return rtu_frame, 0

# ==============================================================================
# 2. 待测 C 语言实现的仿真镜像 (DUT: Device Under Test)
# ==============================================================================

def c_modbus_crc16_calculate(data: bytes) -> int:
    """1:1 镜像 modbus_crc.c 中的计算函数"""
    crc = 0xFFFF
    for b in data:
        index = (crc ^ b) & 0xFF
        crc = (crc >> 8) ^ CRC16_TABLE[index]
    return crc & 0xFFFF

def c_modbus_tcp_to_rtu_pack(tcp_frame: bytes):
    """1:1 镜像 modbus_crc.c 中的转换打包函数"""
    if tcp_frame is None or len(tcp_frame) < 7:
        return None, -1
    
    pdu_len = len(tcp_frame) - 6
    out_rtu = bytearray(pdu_len + 2)
    
    for i in range(pdu_len):
        out_rtu[i] = tcp_frame[i + 6]
        
    crc = c_modbus_crc16_calculate(bytes(out_rtu[:pdu_len]))
    out_rtu[pdu_len] = crc & 0xFF
    out_rtu[pdu_len + 1] = (crc >> 8) & 0xFF
    
    return bytes(out_rtu), 0

# ==============================================================================
# 3. 自动化测试执行器 (Test Suite)
# ==============================================================================

class AutomatedTestSuite:
    def __init__(self):
        self.total_tests = 0
        self.passed_tests = 0
        self.failed_tests = 0

    def assert_case(self, case_name: str, actual, expected, detail: str = ""):
        self.total_tests += 1
        if actual == expected:
            self.passed_tests += 1
            print(f"  [PASS] {case_name}")
            if detail:
                print(f"         └─ 结果: {detail}")
            return True
        else:
            self.failed_tests += 1
            print(f"  [FAIL] {case_name}")
            print(f"         ├─ 预期: {expected}")
            print(f"         └─ 实际: {actual}")
            return False

    def run_crc_tests(self):
        print("\n=== [测试模块 1]: Modbus-RTU CRC16 精度断言测试 ===")
        
        # 用例 1: 工业标准读保持寄存器命令 (01 03 00 00 00 08)
        v1 = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x08])
        res1 = c_modbus_crc16_calculate(v1)
        self.assert_case("用例 1.1 (标准读命令 01 03 00 00 00 08)", 
                         hex(res1), "0xc44", 
                         f"低字节=0x{res1&0xFF:02X}, 高字节=0x{(res1>>8)&0xFF:02X}")
        
        # 用例 2: 工业标准写单寄存器命令 (02 06 00 01 00 03)
        v2 = bytes([0x02, 0x06, 0x00, 0x01, 0x00, 0x03])
        res2 = c_modbus_crc16_calculate(v2)
        self.assert_case("用例 1.2 (标准写命令 02 06 00 01 00 03)", 
                         hex(res2), "0x3898", 
                         f"低字节=0x{res2&0xFF:02X}, 高字节=0x{(res2>>8)&0xFF:02X}")

        # 用例 3: 11 字节长数据多字节连续写命令
        v3 = bytes([0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x12, 0x34, 0x56, 0x78])
        res3 = c_modbus_crc16_calculate(v3)
        golden_res3 = golden_modbus_crc(v3)
        self.assert_case("用例 1.3 (变长连续写寄存器多字节帧)", 
                         hex(res3), hex(golden_res3), 
                         f"真值匹配=0x{res3:04X}")

    def run_tcp_to_rtu_pack_tests(self):
        print("\n=== [测试模块 2]: Modbus-TCP 转 RTU 帧封包测试 ===")
        
        # 用例 1: 完整 Modbus-TCP 读保持寄存器报文
        tcp_in1 = bytes([0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x08])
        expected_rtu1 = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x08, 0x44, 0x0C])
        
        out_rtu1, status1 = c_modbus_tcp_to_rtu_pack(tcp_in1)
        self.assert_case("用例 2.1 (标准 MBAP 7 字节解包与 CRC 追加)", 
                         (status1, out_rtu1), (0, expected_rtu1), 
                         f"生成 RTU 长度={len(out_rtu1)} 字节, 报文={out_rtu1.hex(' ').upper()}")

        # 用例 2: 畸形残缺报文测试 (长度小于 7 字节，验证安全防护)
        tcp_in2 = bytes([0x00, 0x01, 0x00, 0x00])
        out_rtu2, status2 = c_modbus_tcp_to_rtu_pack(tcp_in2)
        self.assert_case("用例 2.2 (残缺畸形帧边界防护，防越界访问)", 
                         status2, -1, 
                         "成功拦截非法报文，返回错误码 -1")

        # 用例 3: 空指针/空输入安全测试
        out_rtu3, status3 = c_modbus_tcp_to_rtu_pack(b"")
        self.assert_case("用例 2.3 (零长度空输入防御测试)", 
                         status3, -1, 
                         "成功防御空输入")

    def run_fuzzing_benchmark(self, iterations=1000):
        print(f"\n=== [测试模块 3]: {iterations} 次随机数据模糊压力测试 (Fuzzing) ===")
        t_start = time.perf_counter()
        
        all_matched = True
        for i in range(iterations):
            random_len = random.randint(7, 120)
            random_pdu = bytes([random.randint(0, 255) for _ in range(random_len)])
            
            c_rtu, c_err = c_modbus_tcp_to_rtu_pack(random_pdu)
            gold_rtu, gold_err = golden_tcp_to_rtu(random_pdu)
            
            if c_rtu != gold_rtu or c_err != gold_err:
                all_matched = False
                print(f"  [ERROR] 发现不匹配帧: index={i}")
                break
                
        t_elapsed_ms = (time.perf_counter() - t_start) * 1000
        per_frame_us = (t_elapsed_ms * 1000) / iterations
        
        self.assert_case(f"用例 3.1 ({iterations} 轮随机变长数据一致性校验)", 
                         all_matched, True, 
                         f"总耗时: {t_elapsed_ms:.2f} ms, 单帧平均耗时: {per_frame_us:.2f} us")

    def print_summary(self):
        print("\n" + "=" * 60)
        print("【自动化测试执行总结报告】")
        print(f"测试用例总计: {self.total_tests}")
        print(f"成功通过数量: {self.passed_tests}")
        print(f"失败拦截数量: {self.failed_tests}")
        pass_rate = (self.passed_tests / self.total_tests) * 100
        print(f"最终通过率  : {pass_rate:.1f}%")
        print("=" * 60)

if __name__ == "__main__":
    runner = AutomatedTestSuite()
    runner.run_crc_tests()
    runner.run_tcp_to_rtu_pack_tests()
    runner.run_fuzzing_benchmark(iterations=1000)
    runner.print_summary()
