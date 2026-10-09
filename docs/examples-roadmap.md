# 公开示例交付路线

本仓库拥有公开教学应用及装配，不包含私有产品算法、客户寄存器表、密钥、PCB 或量产政策。
首发范围为启明 F407ZG、天空星 F407VE、梁山派 F470ZG，并保留 F407VG Discovery。
`069dcf14` 是历史分析基线；当前调用者已使用通用 Runtime。正式 SDK 身份与最终 clean
执行矩阵由 [机器证据](../evidence/platform-refactor-validation.json) 和
[最终软件验证](refactor-validation.md) 维护，不将旧报告套用到新 pin。

## 当前八应用源码

| 应用 | 实际组成与支持组合 | 当前证明边界 |
| --- | --- | --- |
| `boot_blinky` | Runtime、typed GPIO；Native 有限检查，四板裸机／FreeRTOS 心跳 | MCU 初始电平／周期待 HIL |
| `uart_echo` | 四板 MCU-only，typed RX event／TX ticket | Native 无 typed UART provider；线速、取消和溢出实测待 HIL |
| `rtos_pipeline` | Native／四板 FreeRTOS；producer／consumer／heartbeat／statistics 四任务 | 调度、队列负载、栈峰值和长期 MCU 行为待 HIL |
| `industrial_model` | Native Modbus、虚拟 plant 与健康监督 | 不证明 MCU 控制、RS485、物理 watchdog 或执行器安全 |
| `shell_console` | Native 标准流 console、typed GPIO、有限十命令 `--smoke` | 最终 Native 矩阵实际执行；未提供 MCU stdio console |
| `config_roundtrip` | Native／四板裸机和 FreeRTOS；类型、namespace、JSON／binary v2、显式 RAM snapshot | 最终 Native 执行及 8 个 ARM ELF 静态检查通过；没有掉电持久化 |
| `log_uart` | 四板 MCU；caller-owned 4×256 B、单 owner pump、100／1000 ms TX／flush | 最终 8 个 ARM ELF 静态检查通过；buffer lease 和 wire-idle 实板待 HIL |
| `spi_transaction` | Native 有限模型；Discovery SPI1 参考配置裸机／FreeRTOS | Native 三个 case 实际执行、两个 ARM ELF；物理 loopback／DMA 待 HIL |

详细实际定向证据见 [组件例程](component-examples.md) 和 [SPI 例程](spi-example.md)。
已测试的 clean 实现 snapshot Nexus `5498b2b6`／示例 `272200f4` 的 Native 6 项测试和
8 组 ARM 配置、38 个 ELF 全部通过，0 跳过；交付 pin、完整身份和摘要见
[最终软件验证](refactor-validation.md)，新 revision 不自动继承该 snapshot 的结论。
全部物理结果为 **unverified**。三首发板 HIL 只读准入与未配置 station 的设备前拒绝
已经验证，完整 IRQ／DMA、掉电和长期负载 workload runner 仍需实现。
启明／天空星没有 SPI binding，梁山派已有板载 Flash SPI4 binding，但尚无本例 loopback
和器件隔离工装，三板不注册 `spi_transaction`。MCU newlib-nosys 不是已接入的 UART console；
Config／SPI 用 debugger 状态，日志用显式 typed UART sink。

## 保留的分级路线

这些级别是教学目标，不表示每项应用都已实现或具备硬件资格。

| 级别／应用 | 目标与当前平台机制 | 下一项验收 |
| --- | --- | --- |
| E0 `boot_blinky` | Runtime owner／回滚、LED 极性、OSAL | 四板初始电平、周期和错误路径 HIL |
| E1 `uart_echo` | TX ticket、RX event、deadline、cancel／lease | 溢出、raw error、wire TC、cancel／timeout 后重试 HIL |
| E2 `rtos_pipeline` | 有界 OSAL queue／task、slot 与 lifetime token | 满队列政策、栈峰值、真实调度与长期恢复 |
| E3 `spi_transaction` | 同步／queued、child refs、有限 deadline、cancel settlement | Discovery 专用 loopback 工装／波形；其他板独立定义器件协议与接线 |
| E4 `persistent_parameters` | ConfigCore/RAM/Flash、StorageCore/StorageHAL、typed Flash region | 后续新增专用 Flash 示例；显式 layout／scratch／维护窗口，重启和掉电 HIL；默认示例不写 Flash |
| E5 `modbus_reference` | 通用 RTU parser 和 Native 模型 | 通用 transport、标准 UART 帧、微秒时基、DE／drain／abort 适配与真实 RS485 工装 |
| E6 `health_supervision` | 通用监督器、deadline／cycle token／故障事件 | 外部安全输出／健康策略；物理 watchdog／reset 端口和测量另验收 |
| E7 `update_policy_model` | 明确 crypto／storage／update 窄端口 | 后续教学应用显式绑定临时测试 provider、计数器与故障模型；不冒充安全硬件 |
| E8 `secure_update_reference` | 通用状态机和端口，尚无完整 MCU boot/install 链 | bootloader、不可变槽位、MCU crypto／熵源、可信计数器、健康确认与掉电恢复逐板准入 |

E0–E3 的现有源码继续完善软件／实物错误证据。E4–E6 是后续公开组合，E7 可以先做
Native 模型；E8 单独准入。没有日历日期上的安全验收保证，也不新增未承诺的 Ethernet／MQTT 范围。

## 当前公开 API 与调用规则

入口包含 `runtime/nx_runtime.h`，调用 `nx_runtime_bootstrap()` 建立 HAL→OSAL。
`Nexus::Firmware` 显式组成 Board／SoC／startup；它不选择业务任务或组件。
设备包含 `hal/base/nx_device.h`，使用 `nx_device_open(name, class, owner, &ref)`、
`query`、typed action、`close`；owner 来自生命周期稳定的应用对象地址。
发现／metadata 查询不启动硬件，opaque descriptor 不暴露可变 provider 状态。

FreeRTOS 在 bootstrap 后创建 worker 并启动 scheduler；依赖 kernel 同步的组件在任务内
初始化。裸机拥有自己的主循环或 pump。`osal_task_config_t.stack_size` 单位为字节，
后端实际能力／对象／栈预算以 `osal_get_backend_info()` 和有效配置为准。
运行中的 MCU scheduler 不支持全局 teardown 后任意重启；失败不得清除仍被拥有的对象。

UART 使用 `nx_device_uart_submit/poll/cancel/receive_event()`。TX storage 保持到
`poll.settled` 或成功结清的 cancel；poll 的 `NX_OK` 只是查询成功，还需检查 terminal
status，线末位与方向切换需 `wire_idle` 证明。持续 poll 执行 deadline；不能 sleep 一个
timeout 后直接释放 buffer。异常零 ticket 由专用 `nx_device_uart_recover()` 结清后重开。
SPI cancel 仅受理请求，必须 service／poll 到 settled；child 先于 controller 关闭。
一个 UART 只有一个 owner，echo／Shell／Log 组合不能各自打开同一个控制器。

Config 示例显式 `config_set_backend(config_backend_ram_get())`，非加密功能不需要绑定
OpenSSL。Core、RAM、Flash 和 test fake 已分目标；快照 load 前关闭 namespace handles，
恢复后按名字重开。二进制 v2 使用同一 namespace ID map，RAM reset 后丢失。
真实 Flash 组合通过 `nx_device_flash_geometry/block/region_open` 及
`nexus/hal_flash_storage.h` 的 `nx_hal_flash_storage_bind/begin/end/unbind`；
region／权限、统一预算、scratch 和维护窗口归调用者，StorageHAL 不自动选区或解锁。
旧 SoC 特定 flash port 不作为新的公共应用算法接入。

Modbus 的 registers／authorize_write／write_atomic 和 RTU IO 是窄端口；目前协议模型
不等于完成公共 RS485 transport。监督器的 `enter_safe`／`feed_watchdog` 由应用提供。
Update 的 provision／stage／select／confirm／rollback 是机制，stage 前镜像须已安装在
不可变槽位；平台不提供默认产品身份、信任根、制造授权或健康确认任务。
MCU typed I2C 当前明确不支持，Native 成功不能证明 MCU provider 已接入。

## SDK、Board 与资源

外部父工程可使用 `add_subdirectory`，或已实现的可重定位 source package
`find_package(Nexus CONFIG REQUIRED)`，随后调用 `nexus_add_application()`。
`NEXUS_BOARD_DIR` 与 `NEXUS_FLASH_LAYOUT_FILE` 已提供；它们只在维护的 SoC／已校验
资源范围内扩展，不能让任意 PCB／controller 自动可用。image 默认覆盖全物理 Flash，
当前非零 offset 明确拒绝，不提供 A/B bootloader 链。严格 clean 可重定位源码包已完成
3 个 Native consumer 运行、2 个 STM32 C／C++ ELF 检查和 6 项拒绝测试，具体配置和
证明范围见 [最终软件验证](refactor-validation.md)。这是源码 SDK，不提供预编译 ABI 保证。

## 3–6 个月实施容量

- 第 1–2 周：固定依赖和 clean 基线、核对四板与探针、建立真实缺陷复现及 E0 工装。
- 第 3–6 周：完善 UART／任务／SPI 错误合同与 Native 组件例程，获取实际波形和取消证据。
- 第 7–10 周：按已有能力交付 E4–E6，分别验证 Flash 掉电、RS485 方向与监督器恢复。
- 第 11–12 周：收敛完整多仓软件矩阵、预算、文档和 Native 升级政策模型；首板 HIL 单独准入。
- 第 4–6 月：依据真实 MCU 安全端口和硬件进度进入 E8、长期负载和剩余板卡资格。

分工延续约 10 人：技术负责人 1、HAL／OSAL 2、BSP 2、应用／通信 2、QA／HIL 2、
构建发布 1。详细 38 项任务、90／180 人周规划容量及 19 个计划批次保留在
[平台计划](platform-refactor-plan.md)，实际实施状态另由 Nexus execution CSV 维护。

每个示例提交目的、公开 API、配置、接线、资源、有限正常／错误结果和未验证项。
Native 模型、ARM 静态和 HIL 分开记录；硬件结果绑定 PCB／probe／有效配置／镜像摘要。
平台接口与例程迁移由关联提交及准确 SDK pin 交付，不以测试数量或目录数量替代完成定义。
