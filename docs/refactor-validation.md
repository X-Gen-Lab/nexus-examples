# 通用平台重构的软件与 HIL 准备验证

记录日期：2026-10-09（Asia/Shanghai）。用户确认实板暂未接入；本轮交付通用平台软件、独立应用示例、可重定位源码 SDK 和 HIL 工装准备。全部物理资格保持 `hardware_verified=false`。

## 源码身份与最终收敛

平台修复后的交付 revision 为 `5498b2b6988cc1cf28cf4255c5c281d56394d614`，tree 为 `ea178ba746a991c7feb21242215aa1a18e6afb05`，正式 Gitlink 和依赖锁一起更新。此次最后修复按平台校验真实契约目标：Native 使用 `runtime_native_smoke`，ARM 使用 `nexus_contract_firmware.elf`；继续拒绝错误 ELF 架构、类型、目录和替代名称。

最后修复前，干净的 `db6ca0c4` / `fcc17963` 软件快照已经完成平台 1810 项 Native 测试、8 个平台 ARM ELF、6 项示例 Native 测试、38 个示例 ARM ELF，以及源码 SDK 的 3 个 Native 运行、2 个 ARM ELF 和 6 项拒绝检查。GitHub 上该版本的 sanitizer 实际执行 1810 项且没有诊断；后置打包门禁的名称遗漏已据真实失败修复，83 项打包回归和真实 Native/ARM 复核通过。

这些结果属于各自准确 revision。修复后源码的完整矩阵、严格源码 SDK、最新双仓身份和线上结论正在重新绑定；最终记录会写入 [platform-refactor-validation.json](../evidence/platform-refactor-validation.json)。报告中的被测实现提交与后续文档／证据提交分别记录，避免源码 SHA 自递归。不得把前一组结果改名为后一组通过。

## 支持组合与执行边界

| Board | MCU | 软件组合 |
| --- | --- | --- |
| 启明欣欣高配 V3.1 | STM32F407ZGT6 | 裸机、FreeRTOS |
| 天空星青春版 | STM32F407VET6 | 裸机、FreeRTOS |
| 梁山派 | GD32F470ZGT6 | 裸机、FreeRTOS |
| Discovery 参考 | STM32F407VG | 裸机、FreeRTOS |

这四板均为 experimental。平台矩阵每组提供一个通用链接契约 ELF；示例矩阵的启明、天空星和梁山派各为裸机 4 个、FreeRTOS 5 个 ELF，Discovery 为裸机 5 个、FreeRTOS 6 个 ELF。SPI 只注册在 Native 和已定义专用接线的 Discovery 参考配置；梁山派板载 SPI4 Flash 不自动成为 loopback 工装。

Native 的六个有限 CTest 为 `boot_blinky`、`rtos_pipeline`、`industrial_model`、`shell_console`、`config_roundtrip`、`spi_transaction`。MCU-only UART 和 Log 没有借 Native 替身产生运行结论。Config 镜像显式保留 8 KiB newlib heap，使用应用自有、有界 `_sbrk`；该示例要求单 worker 拥有分配器，不承诺多任务或 ISR malloc。ELF 静态 RAM 预算不包含已测动态峰值。

源码 SDK 支持 `find_package(Nexus 0.1.0 EXACT CONFIG REQUIRED COMPONENTS Source)`，消费者选择配置和工具链后重新编译；它不是预编译二进制 SDK。实际搬迁验证在含空格路径和不相关父 Git 仓库中执行，校验来源、完整清单和篡改拒绝。

## 可复核入口

```sh
git submodule update --init --recursive
python3 scripts/test_repository.py
python3 scripts/build.py --preset native-debug --test --parallel 4
python3 scripts/build.py --preset stm32-qiming-baremetal-release --parallel 4
python3 external/nexus/scripts/ci/validate_firmware_elf.py \
  --build-dir build/stm32-qiming-baremetal-release/nexus \
  --report build/stm32-qiming-baremetal-release/firmware-static-contract.json
```

本地联合矩阵显式使用 `/workspace/nexus`，先核验它与正式 Gitlink／lock 的 commit、tree、干净状态和递归依赖身份。这不冒充默认子模块目录的本地运行。示例 GitHub CI 使用正式递归 checkout；每次线上结论独立绑定对应 run 和源码 SHA。

## HIL 工装移交

三首发板实际 `uart_echo` ELF/BIN 的只读准入检查已验证 Board、layout、有效配置、镜像符号及 ELF 重建 BIN 的逐字节一致。空 station 在设备动作和租约取得前拒绝，设备操作列表为空。这属于软件准入检查，没有 program/readback/reset/UART 物理执行。

工装源码、四板 fixture、station 模板和接线记录在 Nexus 的 `scripts/hil/` 与 `docs/implementation/hil-readiness.md`。接入硬件后由 QA/HIL 角色在外部 station 文件签收真实 PCB、UID、probe serial、串口、电气连接和受审 programmer 工具；先 dry-run，再在受控主机显式 `--execute`。共享 lease root 独占 Board、probe 和实际 tty，失败 cleanup 保留隔离状态。

未关闭项仍包括 MCU typed I2C、完整 IRQ/DMA/掉电/长期负载 workload runner、物理 UART/SPI/Flash 时序、栈及动态池峰值、通用 RS485 transport、CAN/Ethernet、非零启动偏移/bootloader 和 MCU crypto／熵源／可信计数器。Shell core 的单例及动态存储也是后续组件任务。模型和静态检查没有把这些项目升级为已交付或硬件合格。

38 个任务、依赖 DAG、19 个计划批次、十人责任配置及 3–6 个月容量仍见 [完整计划](platform-refactor-plan.md)。Nexus execution CSV 是明确注明日期的实现快照；本报告承接最终软件证据，任务的剩余实施和物理条件继续单独跟踪。未合并平台 main，未创建 release/tag。
