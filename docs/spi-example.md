# Typed SPI 有限交易示例

`apps/spi_transaction/main.c` 只使用公共 `hal/base/nx_device.h`、OSAL 和 Runtime。
它不导入 provider 注册头、厂商 SDK、Native 测试 helper，也不注入模拟 RX。
Native 正常 provider 在没有注入数据时按 transaction.length 回显 TX；这是宿主模型行为，
不代表 MCU 物理 loopback、DMA 或 SPI 时序验证。

应用先 bootstrap，再用非零 owner 打开 controller，打开配置被复制的 SPI child。
一共执行三个有限 case：8 B 同步交易并比对 RX，8 B queued 交易并由单一 owner
调用 service／poll 确认 settlement 后比对 RX，以及 queued cancel。
取消 case 先检查接受请求后 poll 仍未 settled，再 service 到 `NX_ERR_CANCELLED`，
最后确认 RX sentinel 没有被修改。没有并行隐藏 worker。

成功时先 close child，之后 close controller；Native 最后关闭 Runtime。
交易 TX／RX、refs 和 ticket 都位于永久静态存储。错误、未知结果或 settlement 超时
锁存失败并保留存储和仍被拥有的资源，不把 cancel 返回 `NX_OK` 当作可以释放 buffer。
`nx_device_spi_recover` 只处理 malformed child-open 的隔离状态，不是通用取消完成接口；
本例不自动选择 recover、discard 或 reset 策略。

## 显式构建选择

```cmake
nexus_add_application(TARGET spi_transaction VERSION "0.1.0"
    SOURCES apps/spi_transaction/main.c EXTRA_DEPS Nexus::HALDevice)
```

| 配置 | CMake compile definitions | 是否创建 target |
| --- | --- | --- |
| Native | `NEXUS_EXAMPLE_SPI_DEVICE="SPI0"`、`NEXUS_EXAMPLE_SPI_CS=3` | SPI0 启用时创建并实际执行 |
| STM32F4 Discovery | `NEXUS_EXAMPLE_SPI_DEVICE="SPI1"`、`NEXUS_EXAMPLE_SPI_CS=0` | SPI1 与参考 Board wiring 启用时 ARM 链接 |
| 启明欣欣、天空星 | 无 | 不创建；尚无 Board SPI binding |
| 梁山派 | 无 | 不创建；已有板载 Flash SPI4 binding，但没有本例 loopback／器件隔离工装 |

CS 是 Board 定义的逻辑标识，不能把 Native 的数值当作 MCU GPIO pin number。
Discovery 当前参考 binding：SPI1，PA5 SCK、PA6 MISO、PA7 MOSI，逻辑 CS0 为 PB0、
CS1 为 PB1。两个 CS 初始为 inactive 高电平；所有实板验证仍为 **unverified**。
当前示例只选 Discovery CS0，不依据芯片具备 SPI 外设推断其他 PCB 的接线。
梁山派平台已绑定 SPI4 的 PF7 SCK／PF8 MISO／PF9 MOSI 与 PF6 CS0；该总线连接板载
Flash，不能直接套用本例 loopback 或把任意回显测试当作板载 Flash 协议验证。

Native CTest 必须执行真实程序并拒绝零测试：

```cmake
add_test(NAME spi_transaction_finite COMMAND spi_transaction)
set_tests_properties(spi_transaction_finite PROPERTIES
    TIMEOUT 15 LABELS "native;example")
```

```sh
cmake --preset native-debug -DNEXUS_SOURCE:PATH=/workspace/nexus
cmake --build --preset native-debug --target spi_transaction --parallel 4
ctest --test-dir build/native-debug --no-tests=error --output-on-failure \
  -R '^spi_transaction_finite$'
```

正式依赖使用 lock 固定的 SDK；路径覆盖用于开发联调。若设备或 typed queue／cancel
capabilities 不存在，程序返回失败并报告操作状态，不降级到 vendor getter 或伪造 RX。

## 生命周期、预算和工装

每次 transaction 的总 deadline 为 100 ms，包括 lock／queue 等待；settlement loop
也有 100 ms 外层预算。service 可能在 provider 内执行有界等待，它不是零等待 API。
源代码只有一份 8 B TX 和 8 B RX，另加静态 refs／ticket 与诊断状态；未分配应用 heap。
FreeRTOS 用一个 2048 B 栈任务，bootstrap 后立即启动调度，交易在任务中执行；
裸机从 main 执行。ISR 或屏蔽中断的上下文不能执行 transfer／service。
FreeRTOS worker 返回后 scheduler 和 TCB 仍被应用拥有，不报告全 MCU teardown 成功。

MCU 不使用 stdout/newlib console。通过 debugger 读取 `spi_example_result`
（-1 未运行、0 通过、1 失败）、`spi_example_completed`（成功应为 3）、
`spi_example_status/failure` 和 `spi_example_buffer_lease_possible`。
最后一个符号保守保留未知／失败路径的借用可能性；buffer 的地址和内容不会因此离开生命周期。

本例的 MCU RX 比对要求显式 MOSI–MISO loopback 工装。接线前核对 PCB 修订、
板上共享 SPI 器件的所有 CS、电平、功耗和总线是否确实由该工装独占；板上从设备
必须保持 inactive 或隔离，不能只依据这份参考表连接跳线。不要连接一个未知从设备
并把 RX 不一致解释为平台失败。工装通过逻辑分析仪观察 SCK、MOSI、MISO、PB0
和最后一位完成，记录真实频率（1 MHz 请求可能映射为更低的合法硬件分频）、
mode 0／MSB 顺序、CS 边界、DMA／IRQ 及任务栈余量。

Native case 通过、ARM ELF 链接成功和实板执行是不同证据。默认例程不写 Flash，
CI 不自动运行任何硬件交易；供电／时序／DMA／误接线及错误恢复需要单独 HIL 记录。

开发 SDK 联合验证已实际执行 Native `spi_transaction_finite`：1 项 CTest、三个交易 case，
零失败、零跳过。Discovery 裸机与 FreeRTOS 两个真实 ELF 均通过向量、强中断绑定、
设备记录和物理内存范围静态校验。报告在
`build/component-check/native/spi-results.xml` 与
`build/spi-check/{discovery-baremetal,discovery-freertos}/firmware-static-contract.json`，
均未建立实板执行结论。正式交付需在最终固定 SDK 及仓库 root CMake 下重跑对应预设。
