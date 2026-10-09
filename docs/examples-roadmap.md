# 公开示例交付路线

本仓库是 Nexus 的外部应用消费者，承载可阅读、可构建、可复现的公开教学应用；私有产品算法、寄存器表、PCB、密钥和量产规则放各自产品仓库。本文以 Nexus `069dcf14f9cb1cd6fc4a869d87bf9827b91ce306` 为基线，计划覆盖启明 F407ZG、天空星 F407VE、梁山派 F470ZG，并保留 F407VG Discovery。硬件资源与限制见 [板卡矩阵](boards.md)。下面的编号是交付级别，不将规划示例或 Native 模型标为已完成 MCU 应用。

## 当前源码交付

| 应用 | 当前实现与支持组合 | 验证边界 |
|---|---|---|
| `boot_blinky` | 已交付源码；Native 有限 LED 状态检查，四板裸机/FreeRTOS LED 心跳 | Native 已运行、ARM 已构建；电气初始电平及周期待 HIL |
| `uart_echo` | 已交付源码；四板 MCU-only，typed RX event 与 TX ticket 逐字节回显 | ARM 已构建；没有 Native typed UART provider，也未通过物理收发、吞吐、时序或完整取消故障测试 |
| `rtos_pipeline` | 已交付源码；Producer/Consumer/Heartbeat/Statistics 四任务，固定队列、同步与统计；Native/四板 FreeRTOS | Native 有限运行已执行、ARM 已构建；MCU scheduler/栈峰值及长期运行待 HIL |
| `industrial_model` | 已交付源码；Native 虚拟 plant、Modbus 和健康监督组合 | Native 模型已运行；不作为 MCU 工业控制、RS485 或物理 watchdog 证据 |

当前实际执行为 3 项 Native 测试及 8 个 ARM 配置、20 个 ELF 的构建/静态检查，最终源码、配置和产物身份以 [验证记录](validation.md) 为准。没有实板 HIL。`rtos_pipeline` 的 MCU `printf` 默认走未绑定 UART 的 newlib-nosys，统计可用 debugger 查看；需要真实串口交互时运行 `uart_echo`，不能把统计打印解释为已接入串口日志。四应用来源与许可另见仓库 NOTICE。

## 分级示例

| 级别 / 建议应用 | 学习目标与实现范围 | 当前平台能提供什么 | 前置条件与验收 |
|---|---|---|---|
| E0 `boot_blinky` | 平台引导、失败回滚、LED 极性、裸机 delay 与 FreeRTOS 单任务 | typed GPIO、Product boot、OSAL；四板已有参考配置 | 当前源码及软件构建已交付；继续实板核对初始电平、周期和断开设备错误 |
| E1 `uart_echo` | TX ticket、RX event、有界接收、总 timeout、取消与缓冲区寿命 | 四板真实 UART 端口；IRQ RX、wire-TC TX；MCU-only | 当前已交付事件回显与编译结果；后续扩展并实测溢出/错误、timeout/cancel 后重试，不宣称完整故障测试已通过 |
| E2 `rtos_pipeline` | Producer/Consumer/Heartbeat/Statistics 四任务、固定队列、生产/消费、超时与资源耗尽 | OSAL task/queue/同步能力和静态 FreeRTOS 池 | 当前源码及软件构建已交付；服务在 scheduler 启动后运行，继续验证满队列策略、栈峰值与真实调度 |
| E3 `spi_transaction` | 同步传输、子设备引用、总预算、有界异步队列、取消请求与完成 | 梁山派 SPI4 与 Discovery SPI1；Discovery 有 DMA 配置 | 先验证夹具、器件身份和 CS；单 worker 调 service；启明/天空星先补 Board 资源 |
| E4 `persistent_parameters` | 参数校验、原子快照、重启读取、失败后重新打开 | storage 与真实内部 Flash 端口；Config manager 可后续组合 | 先用 storage 展示参数记录；Config 当前依赖 security，待拆目标后再提供无密码附加模块版本；绑定分区/scratch，并做重启与掉电 HIL |
| E5 `modbus_reference` | FC03/06/16、CRC、广播、原子写入与授权 | 通用 RTU 引擎和 Native 协议模型 | 先交付协议教学模型；真实 MCU 链路须补时钟、标准串口帧、公共 RS485 适配、收发器夹具与间隔 HIL |
| E6 `health_supervision` | cycle token、job deadline、过期完成拒绝、故障事件与人工 rearm | 无堆、无休眠的通用监督器；LED 和虚拟 fault 注入 | 先做 LED 安全状态教学；物理喂狗/reset 为独立扩展，等待真实端口与复位证据；不宣称工业执行器安全 |
| E7 `update_policy_model` | 镜像身份、签名拒绝、试运行次数、确认、恢复与防回滚状态机 | Native crypto、storage 与 update 端口机制 | 临时测试密钥、明确测试计数器、故障模型；模型与真实安全计数器严格区分 |
| E8 `secure_update_reference` | 真实安装、可信启动、试运行健康确认与掉电恢复 | 当前仅提供状态机和端口，不提供完整 MCU 升级链 | 先完成受保护 bootloader、签名兼容性、槽位/分区、维护中的 MCU crypto/熵源和安全计数器；再逐板 HIL |

E0–E2 先在四板形成共同基线；E3 按已绑定资源做两个实现，避免新增不明引脚；E4–E6 复用纯服务并显式提供公开应用策略。E7 可提前在 Native 完成。E8 单独准入，不为了示例进度把 RAM 计数器、模拟 Flash、CRC 或普通可改写变量当作安全硬件。

## 当前 API 与调用规则

### 启动与设备引用

使用 `product/product.h` 的 `nx_product_boot()`；该函数只建立 HAL→OSAL，不启动 scheduler。设备使用 `hal/base/nx_device.h` 的 `nx_device_open(name, class, owner, &ref)`、`nx_device_query()`、typed action 和 `nx_device_close()`。LED 名称为 `GPIOE3`、`GPIOB2`、`GPIOD7`、`GPIOD12`，应用读取板 profile 的极性后写入安全初始值。

`owner` 使用应用中生命周期稳定、唯一的对象地址。新示例不通过 legacy factory 拿原始实现指针，也不直接调用厂商 SDK。同一 UART 只能有一个设备 owner；UART echo、shell 与日志不能各自打开同一控制器，组合例子应指定一个传输所有者或等待平台的通用复用适配器。

裸机执行主循环，调用 `osal_task_delay()`；不创建任务或软件定时器。FreeRTOS 在 boot 后创建 worker，再调用 `osal_start()`；依赖运行态同步、队列或服务的业务放在 worker 中。`osal_task_config_t.stack_size` 单位为字节，按 `osal_get_backend_info()` 和有效配置检查任务/队列/栈容量。MCU scheduler 正常运行时不提供“任意重启整个平台”的承诺。

### UART 与 SPI

UART 使用 `nx_device_uart_submit/poll/cancel/receive_event()`。TX buffer 保持有效，直到结果 `settled=true` 或取消成功；poll 的 `NX_OK` 只表示查询成功，还需检查 `result.status`。程序必须持续 poll 才能执行 deadline；禁止用 `osal_task_delay(timeout)` 后直接释放 buffer。RX 的 `status/raw_error/has_data/resolution_us` 都参与处理，溢出必须形成可见故障。

SPI 使用 `nx_device_spi_open/transfer/submit/poll/cancel/service/close()`。每个事务包含 TX/RX、长度与有限总 timeout，只有一个 worker 调 `service`。异步 `cancel()` 成功只表示请求受理，仍需等待 terminal settlement；子设备完成和关闭后才关闭控制器。示例用静态或明确持有的缓冲区，不在中断中执行 typed I/O、协议、日志格式化或 Flash 操作。

### 参数、协议与监督

参数示例先使用 `nx_storage_open/load/save()` 展示真实原子快照。公开板适配代码可选择 `nx_stm32f407_flash_port()` 或 `nx_gd32f470_flash_port()`；两者均不含厂商类型，但目前缺少统一的板分区查找 API，因此该选择保留在示例 adapter，不能写进通用应用算法。Config manager 通过 `config_backend_flash_bind()` 绑定调用者分区，并明确 scratch 容量、序列化格式及失败恢复；当前 Kconfig/目标强制依赖 security，不能在关闭 security 的 profile 中直接启用，先完成拆分或显式选择该依赖。真实 RAM backend 属于易失参数示例，不标为持久化。

Modbus 通过 `nexus_modbus_registers_t` 提供一致读取、`authorize_write` 和 `write_atomic`；教学寄存器仅包含计数器、LED 请求和诊断状态。协议层不默认给任意网络写入安全恢复权限。RS485 adapter 提供 `now_us/direction/write/drain/abort_and_settle`，从 typed RX 事件驱动 `nexus_modbus_rtu_feed/poll/rx_error`；`drain` 必须证明最终停止位完成。当前公共 typed UART 未暴露通用 DE 控制，需要平台先提供窄端口，不能借示例绕过租约拿 legacy handle。

监督示例通过 `nexus_industrial_init/poll/report/trip/rearm/event_pop` 使用监督器。应用定义 job/期限、`enter_safe` 与 `feed_watchdog`。第一版用 LED 关灯和明确命名的教学计数器展示状态转换，明确没有物理看门狗资格；物理示例另加真实 watchdog/reset adapter。控制 job、通信 job、Flash 管理操作分别建预算，Flash 擦写不能混入实时控制周期。

### 升级机制与应用策略

`nx_update_init/provision/stage/select/confirm/rollback` 负责验证资格与持久化状态，`stage` 前镜像必须已安装在另一个不可变槽位。示例层提供公开、明确的板/镜像身份、试运行健康策略、可信签名输入、分区组合和 installer binding；通用平台负责端口契约与机制。真实产品的身份、信任根、授权与健康策略仍属于外部私有仓库。当前默认 linker 是单应用加参数区，不能直接当作 A/B firmware 使用；天空星 512 KiB 更需独立布局与容量评估。

## 与平台重构衔接

| 平台先完成的工作 | 对示例交付的作用 |
|---|---|
| 稳定外部应用构建、外部 Board/Kconfig/profile 注入 | 独立仓库不修改 Nexus 源码即可组装公开或私有应用 |
| 框架 core/transport/storage/crypto/test-support 拆目标 | 小示例只启用所需功能，mock 不进入生产默认目标 |
| 通用 monotonic-us 端口与实际精度契约 | 服务不依赖厂商 tick；STM32 具备 RTU/期限演示所需时基 |
| typed UART 标准帧能力、窄 RS485 direction/drain/abort adapter | 物理 Modbus 示例使用统一接口，避免原始指针与控制器双重所有权 |
| Board 资源冲突与分区验证 | 避免示例复用 UART 引脚、错误 CS、覆盖参数区或重配保留 TIMER1 |
| watchdog/reset、MCU crypto/熵源、受保护升级端口 | 支持真实 E6/E8，不能用模型代替这些平台接入 |

当前源码 SDK 已提供外部 `add_subdirectory()`、`NEXUS_CONFIG_FILE`、`Nexus::Product` 与 `nexus_add_application()`。外部自定义应用/板 profile 和可安装 CMake package 仍是后续目标；不声称当前已经完全支持任意外部 PCB。

## 3–6 个月实施顺序

- 第 1–2 周：固化当前依赖、四应用源码与已通过的软件矩阵，QA 核对四板 profile、探针和接线并启动 E0 的物理验证。
- 第 3–6 周：完善 E1/E2 的故障场景并新增 E3；QA 获得 UART/LED/SPI 波形、timeout 和取消证据；平台补通用时基与 RS485 接口。
- 第 7–10 周：交付 E4/E5/E6 教学组合，验证 Flash 重启/掉电、Modbus 丢字节/方向、监督器故障与重新使能；尚未完成的物理能力继续标待接入。
- 第 11–12 周：收敛公共 API、外部构建矩阵、资源预算、文档和故障操作手册；形成可复现的软件示例候选。并行完成 E7 的 Native 故障恢复模型。
- 第 4–6 月：按 MCU 安全端口和硬件进度完成 E8，并扩展实际 watchdog、长期运行与掉电测试。该阶段以准入条件完成为准，不把日历日期当作安全验收。

分工延续 10 人配置：负责人 1 名负责边界与验收；HAL/OSAL 2 名负责平台契约和通用适配；BSP 2 名负责四板资源；应用/通信 2 名负责本示例仓库；QA/HIL 2 名负责模型与实板证据；构建发布 1 名负责依赖锁、矩阵和可复现产物。此分工是 Nexus 项目的实施配置，不要求其他企业使用同样人数。

## 每个示例的交付定义

每个应用随附目的、能力要求、构建命令、有效配置、接线、预期结果、错误路径、资源预算和当前支持等级。提交中只包含公开教学规则；临时密钥限于测试并明确标注。Native 模型、ARM 编译/链接和物理 HIL 三类结果分别记录，不互相替代。

CI 至少构建四板裸机/FreeRTOS 的实际支持组合；RTOS 专属示例不宣称裸机可运行，SPI 仅列已绑定的两个 Board。Native smoke 有限时长和可判断的退出结果；UART 新 ticket 模型缺失时明确 capability 不支持，不能通过测试 helper 偷塞一个模拟生产端口。物理报告绑定 PCB、probe、源码、有效配置、固件哈希和真实执行记录。平台变化与示例 API 迁移使用相互引用的提交/PR，依赖升级后执行受影响矩阵。
