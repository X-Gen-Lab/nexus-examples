# 当前参考板 / Maintained reference Boards

当前主工程只选择以下四个 STM32F407 Board，均使用已审核的 HSE 8 MHz／168 MHz
profile。Board ID 选择精确引脚和选装条件；原理图不能证明本次实物装配。

The four current Boards select the reviewed HSE8/168 MHz profile. Their exact
packages own routing and population choices; no connected PCB has been observed.

| Board ID | MCU | `led0` inactive level | Console |
| --- | --- | --- | --- |
| `stm32f407_qiming_v31` | STM32F407ZGT6 | PE3 high | USART1 PA9/PA10 |
| `stm32f407ve_sky_qingchun` | STM32F407VET6 | PB2 low | USART1 PA9/PA10 |
| `stm32f407zg_qiming_core` | STM32F407ZGT6 | PG13 high | USART1 PA9/PA10 |
| `stm32f407zg_qiming_v41` | STM32F407ZGT6 | PE3 high | USART1 PA9/PA10 |

每板有裸机与 FreeRTOS 两个 preset，只选择 GPIO 和 115200 baud UART IRQ/event。
RX 容量明确为 32 events；优先级为 5。FreeRTOS 配置显式声明 `calls_os=true`，
裸机不调用内核。UART 损坏、溢出或状态事件不被当作可回显字节。

Both backend presets select only GPIO and IRQ/event UART with a 32-event budget.
FreeRTOS explicitly permits the priority-5 notification to call the kernel.
The application rejects damaged reception before authorizing any echo.

核心板／V4.1 最小配置显式选择 SWD，保留 PA13／PA14；可选 NOR、SRAM、显示、触摸、
网络等状态均由 TOML 明确表达。最小工程选择 absent/disconnected 只是使用前提，
不是自动识别实物。ILI9341／XPT2046 型号不授权驱动共享接口或推断其他模块。

Core/V4.1 starters explicitly reserve SWD and declare optional module states.
An absent/disconnected selection is a prerequisite, not a population detector.
Known display and touch controller identities never enable other peripherals.

[`shared_spi_runtime`](../apps/shared_spi_runtime/) 的独立 Board 是 F407ZG 硅片
软件 fixture，声明 SPI1 PA5/PA6/PA7、PE4/PE3 CS。它不等同于任一开发板的已审
双设备接线。传感器、RS485、MCU Flash 分区等仍需应用自有的实际配置。

The shared-SPI Board is a silicon software fixture. Its explicit routes do not
establish physical sensor, chip-select or transceiver connections on these Boards.
Product-owned packages follow the [external Board guide](external-board.md).

HIL fixture 与 Board ID 一一选择，准备状态为 `prepared_unbound`。接入后应核对
PCB revision、完整料号、晶振、供电、探针、串口路径、逻辑电平及安全初始输出，
再执行启动、时钟、UART、IRQ/DMA、停止和故障恢复。Flash／掉电测试使用产品
明确授权的保留区；示例不在启动时自动擦写。

Physical HIL remains **`not_executed`**. Prepared work plans do not establish
observed UID, wiring, electrical timing, watchdog reset or power-loss behavior.
Retired Board examples remain at the fixed source linked from the repository
README and never produce current station plans.
