# Nexus Examples

独立的公开教学应用与板卡接入实例。目标 GitHub 名称为
`X-Gen-Lab/nexus-examples`；远端创建状态以实际交付记录为准。
本次本地交付已完成，GitHub 创建与推送尚未完成，见 [交付状态](docs/delivery.md)。

Nexus 提供通用平台，本仓库提供应用装配。私有产品、客户协议、密钥和
私有 PCB 数据由各自产品仓库维护。

## 当前应用

| Target | Native | 四块参考板裸机 | 四块参考板 FreeRTOS |
|---|---|---|---|
| `boot_blinky` | 有限运行与 GPIO 状态检查 | LED 心跳 | LED 任务 |
| `uart_echo` | 不创建，当前 Native 无类型化 UART port | 事件／票据式回显 | 独立回显任务 |
| `rtos_pipeline` | 有限 producer／consumer 运行 | 不创建 | 队列、同步、LED、统计 |
| `industrial_model` | Modbus 与虚拟 plant／健康监督模型 | 不创建 | 不创建 |

参考板：启明 STM32F407ZGT6 V3.1、天空星 STM32F407VET6 青春版、梁山派
GD32F470ZGT6、STM32F4Discovery。引脚、电气和验证边界见
[板卡说明](docs/boards.md)。模型不会计为 MCU 工业控制或物理看门狗验证。

## 构建

依赖固定为 Nexus `069dcf14`，实际 Gitlink 和完整身份见
[nexus.lock.json](dependencies/nexus.lock.json)。完整 checkout 后使用：

```sh
git submodule update --init --recursive
python3 scripts/build.py --preset native-debug --test
python3 scripts/build.py --preset stm32-qiming-baremetal-release
python3 scripts/build.py --preset stm32-sky-freertos-release
python3 scripts/build.py --preset gd32-liangshan-freertos-release
```

本地已有完整 SDK 时可传 `--nexus-source /absolute/path/to/nexus`；这是显式
开发 override，不替代正式发布的固定依赖检查。构建依赖、ARM 校验和 CI
入口见 [构建集成](docs/build-integration.md)。

当前 SDK 仍通过 `Nexus::Product`／`nx_product_boot()` 提供通用启动。
这是明确的过渡依赖；后续同批更新示例至通用 Runtime bootstrap。

## 重构与交付

首批已通过 3 项 Native 运行检查、8 组 ARM 构建及 20 个 ELF 静态校验；
实板运行尚未验证。平台完整重构分为 38 项任务、19 个提交批次，按 3–6 个月安排。

- [完整平台重构计划](docs/platform-refactor-plan.md)：任务依赖、责任、提交与验收。
- [示例迭代计划](docs/examples-roadmap.md)：基础外设到参数／工业通信／升级实例。
- [验证记录](docs/validation.md)：仅记录实际执行范围。

Nexus 的基础缺陷应修回平台仓库；本仓库更新依赖并验证调用方式。
示例默认不启动 Flash 擦写、制造注入或升级安装。SPI 外设、RS485、存储、
Bootloader 和加密 provider 的后续实例按能力及硬件条件逐项交付。
