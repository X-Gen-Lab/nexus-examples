# 应用与起始工程 / Applications and starters

本仓库只拥有应用组合、执行和产品级示范策略。CPU、SoC、provider、HAL 和通用组件
由固定 Nexus SDK 提供；不在示例中复制实现，也不建立运行时设备注册框架。

Examples own composition and execution. The pinned SDK supplies reusable
mechanisms; applications consume public faces and explicit component targets.

| 应用 / Application | 当前入口与范围 / Current entry and scope |
| --- | --- |
| `platform_reference` | Native 有限 GPIO/UART；四板两后端 / Main matrix reference |
| `uart_owner_runtime` | 多 producer、取消、排空、join、启动失败 / Main matrix owner lifecycle |
| `sensor_spi` | Native BMP280 参考寄存器与 SPI endpoint / Finite host sensor example |
| `journal` | 文件 Flash 的关闭重开恢复 / File-backed persistence |
| `modbus_registers` | Native 授权、CRC、异常回复和 TX 完成 / Register policy |
| `shared_spi_runtime` | Native 两任务两端点；独立 F407 软件 fixture / Shared controller |
| `stream_recovery` | Native 流借用、loss、BUSY stop 和重启 / Backpressure and recovery |
| `industrial_control` | Native 控制、Modbus、Settings、健康及真实 SPI owner / Industrial composition |
| `network_gateway` | Native lwIP/TLS/MQTT 与独立宿主 peer / Network composition |
| `usb_configuration` | Native USBControl 合同；独立 F407 软件 fixture / Configuration application |
| `flash_maintenance` | 经测试执行的应用策略片段，无独立 main / Policy fragment |
| `uart_fifo_acquisition` | 经测试执行的有限 UART workload，无独立 main / Workload fragment |

各目录 README 说明目标、执行范围和借用责任。Native 控制与模拟外部数据必须显式
选入；`industrial_control/native`、`network_gateway/native` 属于宿主支撑，不是
MCU 驱动或完整实物网关。软件 Board 的硅片路由不等于开发板的实际接线。

Per-application READMEs define execution and ownership. Native fixtures model
external input; host peers and POSIX lifecycle are not MCU drivers or physical
network qualification. Software Board routes do not establish fitted wiring.

## 应用层组合片段

[recipes 导航](../recipes/README.md) 按业务任务链接可复用片段，与独立应用共用
公开 SDK 合同，不新增 provider、默认板级路由或后台执行者。

| 任务 | 入口与范围 |
| --- | --- |
| 触摸与控制算法 | [触摸校准](../recipes/control/README.md)；[control_loop](../apps/industrial_control/control_loop.md) 组合真实 ADC stream、EMA、PID、PWM 与控制故障状态 |
| 数据库与快照 | [storage](../recipes/storage/README.md) 对比 KV/TS 的立即写入与 Settings 暂存／提交 |
| 运行与故障诊断 | [diagnostics](../recipes/diagnostics/README.md) 说明复制式观测、日志与故障处理的组合边界 |
| 更新与 USB 存储 | [network](../recipes/network/README.md) 说明更新准备和本地文件系统／MSC 独占交接 |

`control_loop` 是独立 C11 片段，不改变现有 `industrial_control` 的 BMP280/SPI/
Modbus 入口，也不为当前 Native Board 补造 ADC/PWM 连接。应用提供采集周期、
系数、接口和存储，并负责停止、排空与 join。各片段的局部开发测试不能代替正式
SDK 集成；实物校准、时序、掉电和 HIL 仍为 `not_executed`。

## 一致的应用合同 / Common application contract

1. 应用绑定稳定的公开端口和静态存储，显式调用 Platform start。
2. factory 获取已有接口；逻辑角色由 TOML 映射，不由运行时发现。
3. 应用创建任务、启动 scheduler 并推进 service；平台不隐式创建 worker。
4. 停止时关闭准入，继续取消与排空，acquire 观察最终 SETTLED。
5. join producer、owner 和观察者，撤回 IRQ/通知等引用后才回收存储。
6. 只在相应后端允许时停止 Platform；运行中的 FreeRTOS 仍保留内核时钟/Tick。

Applications explicitly own startup, tasks, service progress and shutdown. Factory
lookup allocates nothing and does not start hardware. Accepted descriptors and
buffers remain borrowed until SETTLED; cancellation, timeout and QUARANTINED do
not release them. Failed withdrawal retains storage and recovery responsibility.

## 共享 owner / Shared owners

UART 例中每个 producer 持有自己的 request、operation、payload 和通知，owner
独占 UART start/service/cancel。通知是提示，request predicate 才是完成依据。
Native 运行真实线程；FreeRTOS 任务由应用启动；裸机使用显式协作推进。

Each producer retains independent storage. Only the owner services hardware;
notifications are hints and the settled predicate is authoritative. Native threads,
FreeRTOS tasks and cooperative baremetal progress are distinct execution owners.

启动只记录已成功创建的资源。Native 线程创建后立即运行，后续失败须先关闭准入，
在 owner 继续排空时 join 已创建 producer。FreeRTOS scheduler 尝试启动之前，
未运行的静态任务可按创建逆序显式 discard；一旦尝试 scheduler 启动，不能再按
“从未运行”回滚。scheduler 意外返回进入终止策略并保留全部任务和平台状态。

Partial-startup rollback records successful creations. Running Native threads need
joined drain; unstarted FreeRTOS tasks use the pre-scheduler discard protocol.
After scheduler startup is attempted, its return cannot prove rollback is safe.
The terminal path retains storage and reports the first fault.

隔离策略的 step/deadline 预算只限定升级报告，不授权释放。后续 service 仍实际
执行，直到 provider 真正排空；首故障观察记录不证明输出电平安全。故障输出未绑定
或失败时保持 UNPROVEN，产品拥有实际输出、复位与恢复策略。

Escalation budgets do not revoke loans. Service continues while storage remains
retained. Diagnostic publication never proves electrical safety; product-owned
output and recovery actions require their own review.

## 传感器、协议与存储 / Sensor, protocol and storage

BMP280 使用短轮询 endpoint 和注入的时钟域，无隐藏 worker。Modbus 示例拥有
八个寄存器、写入授权、TX request/event 存储和模拟 DE；共享参考显式为
115200/8N1、每字符十位，不声称物理 RS485 或串行 Modbus 格式资格。

The sensor uses an explicit endpoint and clock. The register example owns its
policy and transport storage. Modeled direction and 115200/8N1 do not qualify a
physical RS485 transceiver or a complete serial Modbus installation.

Journal 选择文件路径、几何、独立区域和 workspace，关闭重开检查恢复。工业组合
在停止、排空和 join 后进入 Settings 维护窗口。Flash 片段要求显式授权、完整保留
sector 和 32 字节 RAM payload，程序单位必须整除 payload；失败不自动重试，也不
推断原子性或自动挑选 MCU Flash 分区。

Persistence geometry, partitions and maintenance are application decisions. File
reopen checks software recovery; physical brownout behavior is separate. Flash
maintenance reports actual progress without retries or atomicity promises.

## 流与软件工装 / Streams and software fixtures

流示例保持消费者借用的数据不变，耗尽容量后观察 LOSS；BUSY stop 停止新生产，
归还借用后再完成 stop 与 restart。失败 release 保留原 token；诊断只解释已复制
事实，不代替归还、排空或服务频率保证。Native 行为不证明 DMA 时序。

Streams retain immutable loans across capacity loss and failed stop. Releasing
loans permits later stop and restart; diagnostics never authorize reclamation.
Native execution does not measure DMA timing.

共享 SPI 工装使用 F407ZG、SPI1 PA5/PA6/PA7、PE4/PE3 两 CS，明确不是四块实板
已验收的双设备连接。显式 POSIX FreeRTOS 源码测试默认关闭；它使用平台开发
harness，并非默认 installed SDK 验收，也不建立 MCU timing 结论。

The shared-SPI fixture owns reviewed silicon routes only. The optional real
FreeRTOS POSIX application is source-only development support, disabled in the
main Native preset. It does not qualify hardware or expand installed acceptance.

## 五种起始工程 / Five starters

| ID | 精确选择 / Exact selection |
| --- | --- |
| `native` | 显式宿主模型 / Explicit host model |
| `qiming` | 启明 STM32F407ZG V3.1 |
| `sky` | 天空星 STM32F407VE 青春版 |
| `qiming-core` | 启明 STM32F407ZGT6 核心板 |
| `qiming-v41` | 启明 STM32F407 高配版 V4.1 |

每个模板有五份自包含文件，由 `templates/manifest.json` 绑定摘要；`.taplo.toml`
提供可选的精确版本编辑器 schema 关联。应用使用
`status`、`console` 强类型角色；问候 TX 真正结清后才停止。复制后不依赖例库中
其他目录。已知 ILI9341／XPT2046 型号不自动授权启用显示或触摸。

Starters bind their own files, assembly and optional pinned editor association;
copied projects have no dependency
on sibling example directories. Strong roles map through TOML. The finite greeting
settles before shutdown; known module identities do not enable peripherals.

构建与 create 命令见[构建指南](build.md)；四板和软件 fixture 边界见[板卡指南](boards.md)。
Build/create commands and hardware prerequisites are maintained in the linked guides.
