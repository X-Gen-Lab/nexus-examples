# GPIO/UART 主参考 / GPIO and UART reference

本目录是主矩阵的 C11 应用。根工程将同一源码与 `configs/` 中的十一份装配组合。
Native 有限执行，四块 F407 板和 NUCLEO-F103RB 各有裸机与 FreeRTOS 配置。
应用显式启动 Platform，取得逻辑 GPIO/UART 接口，推进 TX 到 SETTLED，再按
后端规则停止；RX 损坏、溢出与状态事件不会被当成可回显字节。

This directory owns the main application. The root build selects one of eleven
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

配置与 starter 显式声明已审 LED 极性。启明三种 PCB 低有效，天空星、Native
模型和 NUCLEO-F103RB 高有效。应用使用强类型 GPIO signal 操作状态灯，持有
静态回响缓冲，直至发送结清才可覆盖。裸机主循环和 FreeRTOS 任务由应用创建。

The `status` role uses `nx_factory_gpio_signal_ref` with polarity declared in
TOML. The reference matrix uses UART IRQ. F407 and the Native model expose RX
events. The F103RB software reference uses RX bytes through the existing public
API. The separate `dma.toml` selects STM32F407 TX DMA with RX events.

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
