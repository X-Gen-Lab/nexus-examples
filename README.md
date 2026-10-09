# Nexus Examples

[X-Gen-Lab/nexus-examples](https://github.com/X-Gen-Lab/nexus-examples) 是已创建的私有示例仓库，
初始五个提交已同步至 `main`，初始 [GitHub Actions](https://github.com/X-Gen-Lab/nexus-examples/actions/runs/37894225307)
执行成功。这些历史结果不代表本轮重构与新 SDK pin 的最终验收。

Nexus 提供通用平台，本仓库提供应用装配。私有产品、客户协议、密钥和私有 PCB 数据
由各自产品仓库维护。应用使用公开 typed HAL、OSAL、`Nexus::Firmware` 和
`runtime/nx_runtime.h`，由 `nx_runtime_bootstrap()` 建立 HAL→OSAL；组件和 worker
由应用显式选择。

## 当前八个应用

| Target | Native | MCU 裸机 | MCU FreeRTOS |
| --- | --- | --- | --- |
| `boot_blinky` | 有限运行与 GPIO 检查 | 四板 LED 心跳 | 四板 LED 任务 |
| `uart_echo` | 不创建；Native 无 typed UART port | 四板事件／票据式回显 | 四板回显任务 |
| `rtos_pipeline` | 有限 producer／consumer | 不创建 | 四板队列、同步、LED 和统计 |
| `industrial_model` | Modbus、虚拟 plant、健康监督 | 不创建 | 不创建 |
| `shell_console` | 有限 `--smoke` 或标准流交互 | 不创建 | 不创建 |
| `config_roundtrip` | 非加密类型、命名空间和 RAM 快照 | 四板有限检查 | 四板有限任务 |
| `log_uart` | 不创建；Native 无 typed UART port | 四板固定队列、pump／flush | 四板单 owner 任务 |
| `spi_transaction` | 有限同步／queued echo 与取消 settlement | Discovery SPI1 参考工装 | Discovery SPI1 参考工装 |

四板为启明欣欣 STM32F407ZGT6 V3.1、天空星 STM32F407VET6 青春版、梁山派
GD32F470ZGT6、STM32F4Discovery。启明／天空星尚无 SPI binding；梁山派已有板载
SPI4 Flash binding，但没有本例专用 loopback／隔离工装，因此只为 Discovery 创建
该 SPI 示例。所有实板结果当前均为 **unverified**。资源预算、接线和执行边界见
[板卡说明](docs/boards.md)、[组件示例](docs/component-examples.md) 和 [SPI 示例](docs/spi-example.md)。

## 构建与依赖

`external/nexus` Gitlink 与 [nexus.lock.json](dependencies/nexus.lock.json) 固定正式 SDK 身份。
使用完整 checkout 和锁定工具链，不跟踪浮动分支：

```sh
git submodule update --init --recursive
python3 scripts/test_repository.py
python3 scripts/build.py --preset native-debug --test
python3 scripts/build.py --preset stm32-qiming-baremetal-release
python3 scripts/build.py --preset stm32-sky-freertos-release
python3 scripts/build.py --preset gd32-liangshan-freertos-release
```

联合开发可显式传 `--nexus-source /absolute/path/to/nexus`；它允许验证新 API 调用者，
不跳过正式依赖核验，不作为最终 clean SHA pair 的证据。本轮新 API、八应用和源码 SDK
的最终 clean 消费验收仍待统一记录；准确 pin、实际测试枚举及完整预设矩阵以
[验证记录](docs/validation.md) 与机器证据为准。

当前源码 SDK 已提供 `NEXUS_BOARD_DIR`、`NEXUS_FLASH_LAYOUT_FILE` 和可重定位的
`find_package(Nexus CONFIG REQUIRED)` 源码包。消费工程自行选择配置／工具链并重新编译，
没有预编译 binary SDK 或跨配置 ABI 承诺。接入方式见 [构建集成](docs/build-integration.md)。

## 计划与能力边界

约 10 人、3–6 个月的完整规划保留 38 项任务和 19 个计划批次；计划批次不是已完成提交数。

- [平台重构计划](docs/platform-refactor-plan.md)：历史审计、本轮实现、责任、预算和验收。
- [示例路线图](docs/examples-roadmap.md)：现有八应用与尚未交付的后续实例。
- [交付状态](docs/delivery.md)：远端历史与本轮验收边界。
- [验证记录](docs/validation.md)：实际执行的源码和工件范围。

Native 模型不证明 MCU 电气、DMA 或实时行为；newlib-nosys 不等于 UART console。
MCU typed I2C 仍不支持，RS485／网络及 MCU 安全升级链未在本轮承诺。示例默认不写
Flash、不注入制造秘密、不安装升级。平台基础问题修回 Nexus，本仓库固定新依赖后验证公开调用方式。
