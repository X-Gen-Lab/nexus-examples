# 通用平台重构的软件与 HIL 准备验证

记录日期：2026-10-09（Asia/Shanghai）。用户确认实板暂未接入；本轮交付通用平台软件、独立应用示例、可重定位源码 SDK 和 HIL 工装准备。全部物理资格保持 `hardware_verified=false`。

## 最终源码与实际检查

| 仓库 | 被测 commit | tree |
| --- | --- | --- |
| Nexus | `5498b2b6988cc1cf28cf4255c5c281d56394d614` | `ea178ba746a991c7feb21242215aa1a18e6afb05` |
| nexus-examples | `272200f40eca5b414e4aa97f5c5be67a0659bc78` | `38d8bd2920af45d806ef97ba5e9c6648ed1ba3ee` |

两个仓库在本轮完整矩阵运行前后均 clean，Gitlink、依赖锁、实际源码 commit/tree 和递归 SDK 依赖全部匹配。文档／证据在后续独立提交保存；该提交 SHA 由交付 archive manifest 记录，不把新的文档 HEAD 冒充已测试实现或产生源码 SHA 自递归。

| 检查 | 实际结果 | 原始记录 |
| --- | --- | --- |
| 平台 Native GCC Release | 1810／1810，failure/error/skipped 均 0；完整 configure/build/test 与严格 fresh JUnit | [native.json](../evidence/reports/5498b2b/native.json) |
| 平台 ARM Release | 四板 × 裸机／FreeRTOS，共 8 个 contract ELF 编译、链接与静态检查通过 | [platform_arm.json](../evidence/reports/5498b2b/platform_arm.json) |
| 独立示例 | Native 6／6，8 组 ARM 共 38 个 ELF；76 次 BIN/HEX 重建、8 个 Config heap 合同通过 | [examples.json](../evidence/reports/5498b2b/examples.json) |
| 严格源码 SDK | publishable=true、source_dirty=false；真实搬迁后 3 个 Native 运行、2 个 C/C++ ARM ELF，6 项身份／篡改拒绝及恢复后校验通过 | [source_sdk.json](../evidence/reports/5498b2b/source_sdk.json) |
| 最终发布工具 | 83 项生产打包回归 + actual Native/ARM 各 1 项通过，0 skip | [python_gates.json](../evidence/reports/5498b2b/python_gates.json) |
| HIL 只读准入 | 三首发板真实 UART 工件通过；3 个空 station 均在设备操作前拒绝，operations=[] | [hil_admission.json](../evidence/reports/5498b2b/hil_admission.json) |
| GitHub CI | 平台 CI/Security/Enterprise 与示例 9 job 均成功 | [github.json](../evidence/reports/5498b2b/github.json) |

本地平台 fresh JUnit SHA256 为 `cf523ea9bb697cb2a26bdb00ca0cbdf3fb5ffd402b98721bced4f1c612d91d63`；示例矩阵报告 SHA256 为 `9c2af1bb522eba9dcf3845c57addb14b2a4cad449fb55b88e392fcd6e717774b`。总记录 [platform-refactor-validation.json](../evidence/platform-refactor-validation.json) 保存各原始 JSON 的大小与 SHA256；软件证据包包含真实日志、JUnit、有效配置、Board/layout、工具／运行库身份，以及同批 ELF/BIN/HEX/MAP，archive manifest 逐文件绑定它们。

最终 GitHub [CI 37908075556](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075556)、[Security 37908075125](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075125)、[Enterprise 37908075309](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075309)、[示例 37908306631](https://github.com/X-Gen-Lab/nexus-examples/actions/runs/37908306631) 均成功。Native／Coverage／sanitizer 各实际执行 1810 项，不将重叠次数累计成更多独立用例。SDK/configuration job 实际执行 287 项 Python 回归，严格 Native 下载工件检查通过。PR checkout 为 `9fc34d3c5ba48fc0854823b302e039d24a53d090`，tree 等同平台 HEAD；Pages 部署在 PR 按设计跳过。线上 `installed_source_sdk` 是真实开发 fixture 回归；正式 publishable 包的资格由上述本地 clean strict SDK 验收单独证明。

最后一次真实失败为 `db6ca0c4` 的 Native 后置打包门禁错误要求 ARM contract 名称。`5498b2b6` 按平台校验 `runtime_native_smoke`／`nexus_contract_firmware.elf`，继续拒绝错误架构、ELF 类型、目录和替代名称；完整打包回归与实际工件复核通过后，本轮完整矩阵重新执行。原失败留在历史证据，未改写成通过。

本地门禁报告另引用实际在 `db6ca0c4` 上执行的 214 项 Python 和 9 项 workflow/ELF 检查，并用逐文件／受测定义摘要解释未变化范围和两个 release 模块例外；这些继承记录明确保留旧源码身份。最终线上 job 又独立执行其声明套件，不把历史数字改名或相加。

本地工具为 CMake 4.4.4、Native GCC 14.2.0、ARM GNU 14.3.Rel1（compiler 14.3.1）。本轮未重新下载核验工作区 ARM archive；记录实际 compiler、linker、objcopy 和运行库摘要。线上安装器对固定 ARM archive SHA256 进行校验，各运行环境的实际工具上下文分别保存。

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
