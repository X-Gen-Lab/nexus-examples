# GPIO/UART 主参考 / GPIO and UART reference

本目录是主矩阵的 C11 应用，不是 SDK 复现工具。根工程将相同源码与 `configs/`
中的九份装配组合：Native 有限执行，四块 F407 Board 分别选择裸机或 FreeRTOS。
应用显式启动 Platform，取得逻辑 GPIO/UART 接口，推进 TX 到 SETTLED，再按
后端规则停止；RX 损坏、溢出与状态事件不会被当成可回显字节。

This directory owns the main application. The root build selects one of nine
assemblies. Native injects finite model input; baremetal owns a serialized loop;
FreeRTOS owns a static task/stack and starts its scheduler explicitly. Factory
lookup has no hardware effects. Accepted request/payload storage stays live
through cancellation, timeout and quarantine.

源码与 installed SDK 消费测试复用这些文件。构建命令见[构建指南](../../docs/build.md)，
精确引脚与装配前提见[板卡指南](../../docs/boards.md)。实板状态为 `not_executed`。
The source and installed consumers reuse these files. Board facts and physical
qualification remain separate from linked firmware.

## 有限请求与逻辑 GPIO / Finite request and logical GPIO

`finite_uart.c/.h` 提供这个应用自己的单执行者状态：`start` 只接纳，`step` 最多
推进一次 service，`request_stop` 关闭新准入并请求取消，`finish` 仅在真实结清后尝试
停止端口。它没有 worker、队列或通用设备注册。描述符、payload 和应用控制对象必须
完整分离；接受后一直保持到 SETTLED。BUSY／QUARANTINED 不授权回收。

The caller owns one bounded step and every byte of storage. Cancellation requests
withdrawal; it does not complete it. Failed stop retains the exact application,
request and payload. External publishers and observers must join separately;
this simple reference installs none. Platform and OS shutdown remain separate.

九份配置和五份 starter 显式声明已审 LED 极性：启明三种 PCB 低有效，天空星高
有效，Native 模型使用高有效。业务使用 strong logical signal，不从初始输出位推导
极性。主参考的 echo 缓冲是静态的，未结清前不会再次读取覆盖；正常推进不在传输函数
里忙等。FreeRTOS 任务与裸机主循环仍由本应用显式拥有。

The `status` role uses `nx_factory_gpio_signal_ref`. Physical polarity stays in
TOML; it is not an assumption in application code. The main nine presets retain
IRQ UART. The separate `dma.toml` selects real STM32F407 UART TX DMA while keeping
the RX event responsibilities; successful linking is not physical execution.

从仓库根、已准备工具链和锁定 SDK 后执行 / From the repository root:

```sh
python scripts/verify_sdk.py
cmake -S . -B build/uart-dma-reference -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-gcc.cmake" \
  -DNEXUS_ASSEMBLY_FILE="$PWD/apps/platform_reference/dma.toml" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uart-dma-reference --target platform_reference --parallel 2
```

`finite_uart_google` 执行真实应用 C 的有限预算、拒绝、单步推进、取消／隔离、
失败停止重试及完整控制跨度别名回归。HIL 保持 `not_executed`。
