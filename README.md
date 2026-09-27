# STM32H753 Industrial Gateway (多协议工业边缘网关)

基于 STM32H753ZI 高性能微控制器的工业边缘网关系统，整合以太网 Modbus TCP、RS-485 Modbus RTU、FDCAN 现场总线协议路由，具备多任务看门狗协同监控与 PVD 掉电应急持久化保护能力。

---

## 1. 硬件平台与关键配置

- **主控芯片**: STM32H753ZI (ARM Cortex-M7，最高主频 480MHz，2MB Dual-bank Flash，1MB RAM)
- **供电架构**: LDO 供电模式 (PWR_LDO_SUPPLY，VOS0 超频档位)
- **时钟树配置**: 外部无源晶振 (HSE Bypass 8MHz) 经 PLL 倍频至系统时钟 480MHz
- **以太网接口**: LAN8742A PHY (RMII 接口模式)，LwIP 2.1.2 协议栈 (使能 FreeRTOS 操作系统支持)
- **RS-485 现场总线**: LPUART1 外设，带独立方向控制引脚 (RS485_DIR) 与空闲中断接收机制
- **FDCAN 现场总线**: FDCAN1 经典 CAN 模式 (500 kbps 波特率，标准 ID 过滤)
- **工业安全特性**:
  - PVD (可编程电压检测器): 阈值电平 Level 2，监测 VDD 跌落并通过 EXTI Line 16 触发紧急中断
  - RTC 备份寄存器: 掉电瞬间在零开销中断上下文内将关键运行数据写入 RTC Backup 寄存器池
  - IWDG (独立看门狗): 2000ms 超时周期，配合调试期冻结机制 (__HAL_DBGMCU_FREEZE_IWDG1)

---

## 2. 软件系统架构

系统采用模块化分层设计，划分为外设驱动层 (BSP)、协议路由层 (Protocol)、操作系统任务层 (Tasks)。

```
+-----------------------------------------------------------------+
|                       Modbus TCP Client                         |
+-----------------------------------------------------------------+
                                | Ethernet (Port 502)
                                v
+-----------------------------------------------------------------+
|                   LwIP TCP/IP Stack + FreeRTOS                  |
+-----------------------------------------------------------------+
                                | tcpip_callback
                                v
+-----------------------------------------------------------------+
|               Protocol Router (300 Registers)                   |
|  [0..99] System Info  |  [100..199] CAN  |  [200..299] RS-485   |
+-----------------------------------------------------------------+
          |                                             |
          | Message Queue (8 cmds)                      | Direct Access
          v                                             v
+-------------------------------+              +------------------+
|           BusTask             |              |     NetTask      |
|  - CAN 报文发送               |              |  - 状态监测      |
|  - RS-485 Modbus RTU 发送     |              |  - 健康打卡      |
|  - 1S 软件定时器周期轮询       |              +------------------+
+-------------------------------+                       |
          |                                             |
          +----------------------+----------------------+
                                 | Health Mask Check
                                 v
                       +-------------------+
                       |      SpvTask      |
                       |  - IWDG 集中喂狗  |
                       +-------------------+
```

### 任务划分与优先级矩阵

| 任务名称 | 优先级 | 栈空间分配 | 功能描述 |
| :--- | :--- | :--- | :--- |
| **NetTask** | `osPriorityHigh` | 4096 字节 | 以太网高优先级服务与网络状态监控，定期向看门狗打卡 |
| **BusTask** | `osPriorityNormal` | 2048 字节 | 现场总线调度任务，消费写指令队列，执行 1 秒周期性从机轮询 |
| **SpvTask** | `osPriorityLow` | 1024 字节 | 集中式任务监督器，每 500ms 检查核心任务运行掩码，全员正常方可刷新 IWDG |
| **TimerPoll** | FreeRTOS 软件定时器 | 共享服务栈 | 1000ms 周期执行，触发 RS-485 下行主动查询指令 |

---

## 3. 内存与地址空间映射

### 统一共享寄存器池 (`g_gateway_regs[300]`)

| 寄存器地址范围 | 归属域 | 功能码支持 | 数据含义 |
| :--- | :--- | :--- | :--- |
| `000 ~ 009` | 系统信息区 | 0x03 (读) | 网关固件版本、运行时间高低字、任务运行状态 |
| `010 ~ 099` | 扩展诊断区 | 0x03 (读) | PVD 掉电历史标记、掉电前存活毫秒数 |
| `100 ~ 199` | FDCAN 总线区 | 0x03 (读) / 0x06, 0x10 (写) | 映射现场 CAN 传感器数据，写操作转为 CAN 标准帧下发 |
| `200 ~ 299` | RS-485 总线区 | 0x03 (读) / 0x06, 0x10 (写) | 映射 RS-485 Modbus 从机，写操作入队打包为 RTU 帧下发 |

---

## 4. 目录结构规范

```
STM32H7_Gateway/
├── bsp/                   # 板级外设驱动
│   ├── can.c / can.h      # FDCAN 驱动与报文收发
│   ├── pvd.c / pvd.h      # 电源电压检测与 RTC 备份区读写
│   └── rs485.c / rs485.h  # RS-485 半双工通信与空闲中断
├── protocol/              # 协议编解码与路由引擎
│   ├── modbus_crc.c / .h  # Modbus RTU CRC16 快速查表计算
│   ├── modbus_tcp_server.c# Modbus TCP 502 端口应用层协议解析
│   └── protocol_router.c  # 全局 300 寄存器池与总线写队列分发
├── Tasks/                 # FreeRTOS 业务任务调度
│   ├── tasks_init.c / .h  # 队列、定时器、核心任务创建
│   ├── task_network.c     # 网络层业务任务
│   ├── task_fieldbus.c    # 现场总线收发与从机轮询任务
│   └── task_supervisor.c  # 多任务协同看门狗监视任务
├── Core/                  # CubeMX 生成的内核与启动代码
├── Drivers/               # CMSIS 与 STM32H7xx HAL 库
├── LWIP/                  # LwIP 适配层
├── Middlewares/           # FreeRTOS 与 LwIP 协议栈源码
├── CMakeLists.txt         # 模块化构建规则文件
└── STM32H7_Gateway.ioc    # STM32CubeMX 外设与管脚工程配置
```

---

## 5. 编译与调试指南

### 构建环境需求
- GNU Arm Embedded Toolchain: `arm-none-eabi-gcc 14.3.1` 或兼容版本
- CMake: `>= 3.22`
- 构建工具: `Ninja`

### 命令行编译步骤

```powershell
# 切换至工程目录
cd STM32H7_Gateway

# 生成构建缓存
cmake --preset Debug

# 执行编译链接
cmake --build --preset Debug
```

编译输出目标文件为 `build/Debug/STM32H7_Gateway.elf`。
