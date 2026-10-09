# 参考开发板与资源矩阵

本仓库承载公开教学应用和开发板集成示例；Nexus 仓库提供通用平台、服务和公开参考 BSP，私有产品另建仓库。本文以 Nexus `069dcf14f9cb1cd6fc4a869d87bf9827b91ce306` 为分析基线。引脚来自其板级实现、manifest 和已登记的原始资料，**本轮没有实板测量，四块板均不能标为 HIL 通过**。文档版本、PCB 丝印版本和实际焊装选项分别记录。

## 首发板卡

| 示例板标识 | 芯片与板卡 | LED 与安全初始电平 | 控制台资源 | 时钟参考配置 |
|---|---|---|---|---|
| `qiming_f407zg` | STM32F407ZGT6，启明欣欣高配 V3.1 资料对应板 | PE3，低有效，初始高；PE4/PG9 暂不作为默认示例资源 | `UART0` → USART1，PA9 TX / PA10 RX，AF7 | HSE 8 MHz，SYSCLK 168 MHz |
| `sky_f407ve` | STM32F407VET6，立创天空星青春版 | PB2，高有效，初始低 | `UART0` → USART1，PA9 TX / PA10 RX，AF7 | HSE 8 MHz，SYSCLK 168 MHz |
| `liangshan_f470zg` | GD32F470ZGT6，立创梁山派 | PD7，高有效，初始低 | `UART0` → USART0，PA9 TX / PA10 RX，AF7 | HXTAL 25 MHz，SYSCLK 200 MHz |
| `discovery_f407vg` | STM32F407VGT6，STM32F4DISCOVERY / MB997 参考板 | PD12，高有效，初始低 | `UART1` → USART2，PA2 TX / PA3 RX，AF7 | HSE 8 MHz，SYSCLK 168 MHz |

示例板标识是本仓库的入口名，平台现有 Board ID 分别为 `stm32f407zg-qiming-v31`、`stm32f407ve-sky-qingchun`、`gd32f470zg-liangshan`、`stm32f4discovery-mb997`。Nexus 的逻辑 UART 索引不等于厂商 USART 编号，Discovery 不应照搬其他板的 `UART0`。

接线前核对：

- 启明板控制台资料使用 MAX232 和 P14 跳线；板载 RS232 连接器应接匹配的 RS232 适配器。若直接接 MCU 引脚，需先核对隔离方式与跳线，不能把 TTL 适配器接在 RS232 电平端口上。
- 天空星青春版没有内置 USB-UART 桥，调试排针存在未焊装选项；须记录实际排针和外部 3.3 V UART 桥。不得套用 VG 高配版本的容量或器件表。
- 梁山派 MCU 侧使用 3.3 V UART；实际 DAPLink/桥接器、连接器路由与 PCB 版本由夹具记录确认。
- Discovery 先使用 PA2/PA3 外接 UART 夹具。不能假定所有 MB997/ST-LINK 修订均提供已连通的虚拟串口。

## SPI 与可选扩展

| 板卡 | 当前 SPI 资源 | 可实施示例 | 尚未具备的能力或证据 |
|---|---|---|---|
| 启明 | baseline 禁用 SPI，未绑定板级资源 | 先完成 GPIO/UART/OSAL | SPI 引脚、片选、跳线与器件身份待验证；有 W25Qxx 教程不等于当前已接入 |
| 天空星青春版 | baseline 禁用 SPI，板载 Flash 为未焊装配置 | 外接器件方案可作为后续独立 profile | 外接 SPI 引脚、片选、冲突校验与器件驱动待接入 |
| 梁山派 | `SPI4`，PF7 SCK / PF8 MISO / PF9 MOSI，AF5；CS0=PF6，低有效 | 轮询传输、有界队列、只读 JEDEC 探测教学例 | 无现成外部 NOR 存储驱动；W25Q64 为资料中的器件系列，实物型号及读写/掉电行为待验证 |
| Discovery | `SPI1`，PA5 SCK / PA6 MISO / PA7 MOSI，AF5；CS0=PB0、CS1=PB1，低有效 | 显式外部 slave/回环夹具；同步、队列与 DMA 对比 | 外部接线、真实 DMA/片选时序和取消后收敛待 HIL；不能当成板载 Flash |

梁山派默认 SPI 是轮询控制器加任务驱动队列，没有 DMA 支持声明。Discovery 配置可启用 DMA2 TX stream3/channel3、RX stream0/channel3，但应用仍必须查询能力、保留借用缓冲区并驱动 `nx_device_spi_service()`。不要把厂商外设存在、公共头文件存在解释为已支持 ADC、CAN、Ethernet、LCD、I2C、USB、SD 卡或 RTC 教程；这些另列平台接入任务后再进入示例矩阵。

四板默认均未绑定工业执行器或可直接使用的 RS485 链路。梁山派提供显式启用的外接 PB1 DE 参考接线，默认关闭；其余板需要独立的、验证后的收发器和方向资源 profile。RS485 教学应用应使用隔离/供电匹配的外部夹具，并记录 DE/RE、终端与偏置连接。

## 内存与参数演示

| 板卡 | 物理内部 Flash | 当前应用区 / 保留参数区 | 默认主 RAM |
|---|---:|---|---:|
| 启明 / Discovery | 1 MiB | 768 KiB / `0x080C0000` 起 256 KiB，两个 128 KiB 擦除单元 | 128 KiB |
| 天空星青春版 | 512 KiB | 256 KiB / `0x08040000` 起 256 KiB，两个 128 KiB 擦除单元 | 128 KiB |
| 梁山派 | 1 MiB | 1008 KiB / `0x080FC000` 起 16 KiB，四个 4 KiB 擦除单元 | 192 KiB |

STM32 的 64 KiB CCM、GD32 的 256 KiB ADDRAM 和 64 KiB TCM 不纳入当前默认分配，也不能直接当作已初始化、DMA 可达的普通 RAM。静态链接占用与实测栈峰值分别报告。

参数示例可在维护窗口调用现有真实内部 Flash 端口与 `nx_storage_open/load/save`。STM32 当前保留区仅容纳一套使用两个独立擦除单元的快照；不能直接再加一套独立的升级状态快照。梁山派四页可规划两套 8 KiB 快照，但仍须显式分区、串行化和容量核对。以上均是单应用镜像布局，**不是已实现 A/B 升级布局**。STM32 Flash 头中的 sector10/11 注释不能套到 VE 型号，实际 VE 几何为 sector6/7，需随平台修正注释。

## 示例与板卡矩阵

当前已交付 `boot_blinky`、`uart_echo`、`rtos_pipeline`、`industrial_model` 四个应用的源码。实际支持组合已执行 3 项 Native 测试及 8 个 ARM 配置、20 个 ELF 的构建/静态检查，最终身份与结果见 [验证记录](validation.md)；没有执行实板 HIL。“可接入”仅表示平台能力可用于后续示例开发，不能替代示例实现或实板验收。

| 示例层级 | 启明 | 天空星 | 梁山派 | Discovery |
|---|---|---|---|---|
| `boot_blinky` 裸机/FreeRTOS | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL |
| `uart_echo` MCU TX/RX 事件 | 已 ARM 构建；核对 RS232 | 已 ARM 构建；外部 UART 桥 | 已 ARM 构建；核对桥路由 | 已 ARM 构建；UART1 夹具 |
| `rtos_pipeline` FreeRTOS 四任务 | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL | 已 ARM 构建，待 HIL |
| SPI 同步/队列 | 待板级资源 | 待板级资源 | 可接入；实物 Flash 待核对 | 可接入；外部夹具 |
| 真实 Flash 参数重启恢复 | 可接入；维护窗口及掉电 HIL 待做 | 同左；应用仅 256 KiB | 可接入；页擦除 HIL 待做 | 可接入；维护窗口及掉电 HIL 待做 |
| `industrial_model` Modbus/健康教学模型 | 仅 Native，非本板固件 | 仅 Native，非本板固件 | 仅 Native，非本板固件 | 仅 Native，非本板固件 |
| 物理 Modbus RTU/RS485 | 待微秒时钟、标准帧配置、DE 与 HIL | 同左 | 待标准帧配置、公共 DE 适配与 HIL | 待微秒时钟、标准帧配置、DE 与 HIL |
| 监督器 LED 故障演示 | 可接入；非工业安全输出 | 同左 | 同左 | 同左 |
| 物理 watchdog/reset 监督 | 待已验证 watchdog/reset 端口 | 同左 | 同左 | 同左 |
| 安全升级模型 | Native 中开发与验证机制 | Native 中开发与验证机制 | Native 中开发与验证机制 | Native 中开发与验证机制 |
| MCU 安全安装、试运行与回滚 | 待 bootloader/槽位/信任/计数器/HIL | 同左，额外受 512 KiB Flash 限制 | 同左；外部 Flash 不等于安全槽位 | 同左 |

STM32 RX 当前默认时间戳精度为 1000 µs，不能据此宣称满足高速 RTU 的 750/1750 µs 间隔判定。梁山派已有 1 µs TIMER1 时间基准，但 TIMER1 为平台保留资源，不能被示例重新配置；其 UART baseline 固定 8N1，也不能直接宣称标准 RTU 帧格式合规。`nx_uart_rx_event_t.resolution_us`、错误/溢出事件、真实 TC、DE 时序均进入链路验收。

`uart_echo` 为 MCU-only，Native 当前没有 typed UART event/ticket provider。它已交付逐字节事件回显与 TX settlement 检查，但串口吞吐、物理时序、错误/溢出和取消故障注入尚无实板通过记录。`rtos_pipeline` 包含 Producer、Consumer、Heartbeat、Statistics 四任务；MCU 默认 newlib-nosys 未将 `printf` 绑定到 UART，统计可通过 debugger 查看，实际串口教学入口为 `uart_echo`。

## 资料与证据

资源事实来自 Nexus 的 [启明 manifest](https://github.com/X-Gen-Lab/nexus/blob/069dcf1/boards/stm32f407_qiming_v31/manifest.json)、[天空星 manifest](https://github.com/X-Gen-Lab/nexus/blob/069dcf1/boards/stm32f407ve_sky_qingchun/manifest.json)、[梁山派 manifest](https://github.com/X-Gen-Lab/nexus/blob/069dcf1/boards/gd32f470_liangshan/manifest.json) 和 [Discovery manifest](https://github.com/X-Gen-Lab/nexus/blob/069dcf1/boards/stm32f4discovery/manifest.json)。Discovery UART 由对应 `uart.c` 补充。原始资料来源为用户提供的 [启明仓库](https://github.com/petercwq/STM32407)、立创 [天空星资料](https://wiki.lckfb.com/zh-hans/fdb/skystar.html) 和 [梁山派资料](https://wiki.lckfb.com/zh-hans/lspi/)。本轮未重新联网核验这些资料与当前实物修订。

每次实板运行记录 PCB/MCU/晶振/焊装、probe UID、接线照片、平台与示例 commit、有效配置、工具链、ELF/map/hash、串口和波形、实际测试结果。公共示例通过后仍不代表某个私有产品获得工业安全或长期支持资格。
