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
