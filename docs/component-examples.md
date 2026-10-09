# 组件组合示例

应用入口、命令、配置键和发送任务属于本仓库。SDK 提供通用 Runtime、设备契约及组件 targets。
三个示例只包含公开头文件，不使用厂商类型、内部注册描述符或测试 fake。

| target | 后端 | 显式 `EXTRA_DEPS` | 行为 |
| --- | --- | --- | --- |
| `shell_console` | Native | `Nexus::Shell` | 真实标准输入输出、typed GPIO、有限 `--smoke` |
| `config_roundtrip` | Native、MCU 裸机／FreeRTOS | `Nexus::ConfigCore Nexus::ConfigRAM` | 非加密类型、命名空间、JSON／binary v2、RAM 快照恢复 |
| `log_uart` | MCU 裸机／FreeRTOS | `Nexus::LogRuntime Nexus::LogUART` | caller-owned 四槽队列、有界背压、显式 pump 与 wire-idle flush |

`nexus_add_application` 自身组合 `Nexus::Firmware`，包含选定 Board、SoC、启动与 Runtime。
选择关闭的组件时对应 target 不存在；不能通过隐式 include 路径补齐依赖。

```cmake
nexus_add_application(TARGET shell_console VERSION "0.1.0"
    SOURCES apps/shell_console/main.c EXTRA_DEPS Nexus::Shell)
nexus_add_application(TARGET config_roundtrip VERSION "0.1.0"
    SOURCES apps/config_roundtrip/main.c apps/config_roundtrip/heap_bounds.c
    EXTRA_DEPS Nexus::ConfigCore Nexus::ConfigRAM)
# Native adds heap_bounds_test.c; MCU adds its app-owned newlib_sbrk.c.
nexus_add_application(TARGET log_uart VERSION "0.1.0"
    SOURCES apps/log_uart/main.c EXTRA_DEPS Nexus::LogRuntime Nexus::LogUART)
```

第一项只在 Native 配置创建，第三项只在 MCU 配置创建。MCU console 由应用显式定义：
Discovery 为 `NEXUS_EXAMPLE_CONSOLE_DEVICE="UART1"`，其他三块参考板为 `"UART0"`。
逻辑设备与实际引脚的对应关系见 [板卡与接线](boards.md)。

## Native 执行

Native 配置启用 `FRAMEWORK_SHELL`、`FRAMEWORK_CONFIG` 和 `SERVICE_SECURITY`；
`CRYPTO_PROVIDER_OPENSSL=n`。ConfigCore 使用中性 crypto facade，但本例不选择 crypto provider，
也不创建加密配置。RAM 后端通过公开的 `config_ram_backend.h` 和 `config_set_backend` 显式绑定。

```sh
cmake --preset native-debug -DNEXUS_SOURCE:PATH=/workspace/nexus
cmake --build --preset native-debug --target shell_console config_roundtrip --parallel 4
ctest --test-dir build/native-debug --output-on-failure --no-tests=error \
  -R '^(shell_console_finite|config_roundtrip_finite)$'
```

上面的开发覆盖指向本机 SDK checkout；正式验证应使用依赖锁固定的 `external/nexus`。
CTest 注册须使用以下实际可执行命令，超时 15 秒：

```cmake
add_test(NAME shell_console_finite COMMAND shell_console --smoke)
add_test(NAME config_roundtrip_finite COMMAND config_roundtrip)
set_tests_properties(shell_console_finite config_roundtrip_finite
    PROPERTIES TIMEOUT 15 LABELS "native;example")
```

Shell smoke 逐条执行十个命令：四种 LED 操作、有效／无效 delay、错误 LED 参数、
多余参数、info 和 quit。每条检查 handler 执行次数、返回状态和 typed GPIO 读回；
另外检查重复注册和空命令注册。它不只检查进程退出码。
交互模式的 `fgets` 在应用中阻塞，Shell 的 read callback 只消耗已缓冲的数据并立即返回。
输入行上限 126 字节，超长行拒绝执行；退出后注销 Shell 并释放 GPIO／Runtime。

Config 检查七种值的精确读回，两个命名空间的同名键隔离，小缓冲区容量报告、
错误类型、截断 CLEAR import 保留现值、JSON 和 binary v2 恢复，再验证 RAM
`commit → 修改三个命名空间 → load` 恢复十一条记录。binary v2 使用既有 namespace ID，
须在相同命名空间映射下导入。RAM snapshot 在 deinit 或复位后消失，不提供掉电持久化。
快照 `load` 会替换命名空间映射，示例先关闭所有 namespace handles，再恢复并按名字重新打开。

## MCU 执行与资源

四块参考板均使用 Nexus 已提供的 typed GPIO／UART 实现；支持裸机与 FreeRTOS。
Config 和 Log 的组件初始化及调用发生在 main（裸机）或已调度的单一应用任务
（FreeRTOS）。ISR 不调用这些 API。FreeRTOS task 的返回不代表整个 MCU Runtime 已关闭；
任务 TCB 和内核的生命周期由应用管理。

| 资源 | `shell_console` | `config_roundtrip` | `log_uart` |
| --- | --- | --- | --- |
| 应用缓冲区 | 输入 128 B；Shell 128 B command buffer，history 8、commands 16 | JSON 2048 B、binary 2048 B | 固定 4 × 256 B TX 数据，另加 `log_uart_backend_t` 元数据 |
| MCU／Native 配置容量 | 不支持 MCU 标准流版本 | key/default 32、key length 32 B、value/default 64 B、snapshot scratch 2048 B；Core/RAM 使用同一配置 | `LOG_UART_QUEUE_DEPTH=4`、`LOG_UART_MESSAGE_SIZE=256`，编译断言 |
| 应用 FreeRTOS worker | 无 | 一个 2048 B 栈任务 | 一个 2048 B 栈任务、同步 logger 所用 OSAL mutex；无 logger async worker |
| 操作预算 | delay 1–1000 ms | 单 owner 有界容量；snapshot 不涉及 Flash | 每次 UART TX 100 ms、每次 flush 1000 ms |

容量符号必须通过 Kconfig 配置，使组件和调用者使用同一有效配置；只缩小应用中的
`config_manager_config_t` 不会缩小 SDK 静态容量。应用首先包含 `nexus_config.h`。
以上是声明的容量，不是总 RAM 或栈峰值测量；总量应读取实际 ELF/MAP，栈峰值需要 HIL。

Config 的原子 import 会临时分配 replacement 表、原值表及值副本；仅保留 linker heap
容量并不构成有界 C 分配器。旧 ELF 中工具链的 libnosys `_sbrk` 只增长 break，
没有检查声明的 heap／stack 边界。MCU 本例显式加入应用拥有的 `newlib_sbrk.c`，从两个
维护中的 GNU linker 实际符号 `_end`（heap 起点）和 `_Min_Heap_Size`（绝对大小）
建立 8192 B 有界 arena。它不会进入栈预留或剩余空闲 RAM；扩展超界返回 `ENOMEM`，
收缩越过起点返回 `EINVAL`，正负溢出失败均保持 break。默认例程不依赖 libc stdout。

裸机所有 allocator 调用由 main 串行执行；FreeRTOS 只由已经调度的 Configuration
worker 绑定和使用。`_sbrk` 拒绝 ISR、被屏蔽的上下文及其他 worker 扩堆，但这只约束
arena 增长；malloc 已有 free-bin 和 free 的元数据操作可能不调用 `_sbrk`。
整个示例禁止其他任务／ISR 调用 malloc-family，不提供通用 newlib 多任务锁或 ISR 安全保证。
Native 使用宿主分配器完成相同受限 Config 用例，并在已有有限 CTest 内运行同一边界算法的
实际 grow/shrink/full/OOM、PTRDIFF 边界和前后 canary 模拟检查；它不执行 MCU newlib。
Debugger 的 `config_example_heap_{begin,end,break,high_water}` 记录 arena break，
不等于 malloc live bytes，也不能替代实板堆／栈峰值验证。

下面是调整示例容量与 libc adapter 之前的历史开发数据，不能用作当前 MCU 执行证据。
开发中的 SDK 联合验证曾实际完成 Native 两项 CTest（0 失败、0 跳过；禁用 OpenSSL 查找），
以及启明欣欣裸机、梁山派 FreeRTOS 的两应用共四个 ARM ELF 静态检查。
以下是这两份实际有效配置的 Flash 加载／主 RAM 预留量，包含链接器栈／heap 预留，
不代表其他配置或运行峰值。ARM MAP 中 `uart_sink` 为 1152 B（1024 B 数据加元数据）。

| 开发验证配置 | `config_roundtrip` Flash／RAM | `log_uart` Flash／RAM |
| --- | --- | --- |
| 启明欣欣裸机 | 73048 B／81856 B | 45292 B／15656 B |
| 梁山派 FreeRTOS | 74208 B／97752 B | 52304 B／33872 B |

联合验证证据位于 `build/component-check/native/component-results.xml` 和
`build/component-check/{qiming-baremetal,liangshan-freertos}/firmware-static-contract.json`。
后者均明确 `hardware_verified=false`。它们保留为历史开发证据。本轮已测试的 clean
实现 snapshot Nexus `db6ca0c4`／示例 `fcc17963` 完整矩阵中，Native 6 项 CTest 全部通过，
其中包含 Shell 与 Config；Config、Log 各有四板裸机／FreeRTOS 共 8 个 ELF 静态检查
通过，Config 的 8 个 ELF 均核验应用提供的强 `_sbrk` 与 8192 B heap 预留。
详见 [最终软件验证](refactor-validation.md) 和
[机器证据](../evidence/platform-refactor-validation.json)，后续交付 pin 及其实际验证单独
记录，不将上述 snapshot 写成新 HEAD 的执行结论。MCU 用例没有实际运行。

MCU Config 不假定 newlib `_write` 已重定向为 UART console，也不尝试标准流输出。
它执行同一有限检查，在 debugger 中观察 `config_example_result`（-1 未运行、0 通过、1 失败）、
`config_example_checks` 和 `config_example_failure`。Native 使用实际 stdout，并检查写入／flush 错误。

Log 每批先接受四条记录，第一次再提交第五条并验证 `LOG_ERROR_FULL` 与 dropped 计数。
随后应用调用一次非等待的 `service`，再执行有限 `flush`；共发送三个批次十二条记录。
logger dispatch 只复制到固定队列；没有隐式线程、timer 或 UART stdout retarget。
UART provider 借用的是永久 `uart_sink.messages`，发送者的格式化栈不被借用。

成功时先验证 completed=12、dropped=1、failed=0、pending=0、buffer_leased=false，
再 unregister／close 和 logger deinit。第五条 probe 应被拒绝，线上只能看到 sequence 0–11。
失败保留静态 sink、logger 所有权和任何尚未结束的 provider buffer lease，锁存
`log_uart_example_result/status/failure/stats`；应用随后可决定恢复、丢弃或复位。
本例不自动选择此类产品策略，不把超时当作可以释放缓冲区的证据。

## HIL 验证边界

所有实板结果当前为 **unverified**。ARM 构建与 ELF 静态检查不能验证串口电气连接、
中断时序、DMA、供电或实际栈余量。默认示例不绑定 Flash backend，不调用 Flash 擦除写入。

实板工装应记录：板卡修订、适配器／探头、有效配置摘要、SDK 和示例 commit、供电、
串口电平及设置。先核对 [板卡文档](boards.md) 中的 RS232／TTL 和跳线，不能直接把
RS232 信号接到 MCU 引脚。使用配置选定的 UART 参数，记录实际捕获字节和时间戳。

对 Log 用例，捕获十二条连续 sequence、确认无 queue-full-probe，再通过 debugger
读取结果和 stats，检查缓冲区已 settle、TX 引脚最后一位完成。分别在裸机与 FreeRTOS
验证栈峰值和重启。对 Config 用例，读取 result/checks/failure，复位后重新运行并确认
RAM 数据没有被误认为持久化。实际中断／错误注入与超时恢复需独立记录证据。

SDK 中旧 `shell_demo`、`config_demo` 的公开调用覆盖由本仓库这两个示例承接。
旧直接调用 vendor API 的 `stm32_blinky`、`stm32_config_test` 不作为新的公开平台用例。
