# 交付与远端同步状态

记录日期：2026-10-09（Asia/Shanghai）。独立示例仓库已创建；本轮交付软件和 HIL 工装
准备。实际硬件暂未接入，规划任务和软件结果不代表实板资格。

## 已同步的初始历史

远端为私有 [X-Gen-Lab/nexus-examples](https://github.com/X-Gen-Lab/nexus-examples)，
默认分支 `main`；本地 `origin` 指向同一仓库。初始五个提交已同步：

| 初始批次 | 提交 |
| --- | --- |
| 固定源码 SDK、外部 CMake 工程、四个应用 | `2a7b555` |
| 依赖身份核验、薄构建入口、CI 定义 | `7196841` |
| 板卡说明、示例路线图、38 项任务与 19 个计划批次 | `e1453ef` |
| 绑定旧 SDK 的 Native／ARM 实际软件证据 | `24ef09c` |
| 初始交付记录 | `1d517fe` |

初始 [GitHub Actions run 37894225307](https://github.com/X-Gen-Lab/nexus-examples/actions/runs/37894225307)
已成功。它对应当时的提交与 SDK，不能证明本轮 Runtime／组件／SPI 调用者或新 pin 已完成线上验收。
之前记录的仓库创建／连接限制是历史过程，已不再作为当前交付阻碍。

## 本轮实现范围

调用者已迁移至 `nx_runtime_bootstrap()`／`Nexus::Firmware`，增加 `shell_console`、
`config_roundtrip`、`log_uart`、`spi_transaction`，与原四应用形成八应用源码集合。
组件例程只消费公开 headers／targets；SPI 使用 Native 正常 echo 模型并在 Discovery
参考配置链接。启明／天空星未绑定 SPI，梁山派板载 Flash 的 SPI4 尚无本例专用
loopback／隔离工装，因此三板不注册该示例 target。详细资源、有限执行和错误路径见
[组件说明](component-examples.md) 与 [SPI 说明](spi-example.md)。

Nexus 本轮已经修改通用 Runtime、HAL、OSAL、Board／Flash、组件目标、构建与源码 SDK。
历史 `069dcf14` 只保留为重构前审计与首批验证身份，不再描述为本轮唯一平台实现。
正式依赖由 `external/nexus` Gitlink 和 `dependencies/nexus.lock.json` 一起更新、核验。
平台本轮 22 个变更提交已推送 [PR #3](https://github.com/X-Gen-Lab/nexus/pull/3)，
示例本轮四个实现／依赖提交已同步 `main`，随后单独提交最终文档与机器证据。

最终实际验证的 clean 源码为：

| 仓库 | 已测试实现 commit | tree |
| --- | --- | --- |
| Nexus | `5498b2b6988cc1cf28cf4255c5c281d56394d614` | `ea178ba746a991c7feb21242215aa1a18e6afb05` |
| nexus-examples | `272200f40eca5b414e4aa97f5c5be67a0659bc78` | `38d8bd2920af45d806ef97ba5e9c6648ed1ba3ee` |

平台 Native **1810／1810**、8 组平台 ARM contract ELF、示例 Native **6／6** 及
8 组 ARM 配置共 **38 个 ELF** 全部通过；执行测试失败／错误／跳过均为 0。
严格 clean、publishable 源码 SDK 完成实际搬迁、3 个 Native 运行、2 个 STM32 C／C++
ELF 检查与 6 项拒绝测试。三首发板 `uart_echo` 的只读 HIL 准入通过，空 station
全部在访问设备前拒绝。结果和工件摘要见 [最终软件验证](refactor-validation.md) 及
[机器证据](../evidence/platform-refactor-validation.json)。

最终远端结果为：

- [平台 CI 37908075556](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075556)：成功；Native／Coverage／sanitizer 实际各执行 1810 项，严格报告通过，打包与 SDK 门禁通过。
- [Security 37908075125](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075125)：4 个 job 成功。
- [Enterprise 37908075309](https://github.com/X-Gen-Lab/nexus/actions/runs/37908075309)：成功。
- [示例 CI 37908306631](https://github.com/X-Gen-Lab/nexus-examples/actions/runs/37908306631)：`272200f4` 的 9 个 job 全部成功，使用正式递归子模块 checkout。

PR 工作流实际 checkout 为合成 merge `9fc34d3c5ba48fc0854823b302e039d24a53d090`，
tree 与平台 `5498b2b6` 完全一致；PR 的 Pages 部署按设计跳过，不是测试跳过。
最终文档／证据提交不改应用、配置或 SDK pin，也不冒称新的 Git HEAD 已重新执行上述测试。
交付 archive manifest 单独记录文档 HEAD 与被测源码 pair，避免 SHA 自递归。

`db6ca0c4` 曾因 Native 打包校验仍要求 ARM 固件名而使 CI 失败；该失败没有改记为成功。
最后提交按平台限定真实 contract 名称，保留架构／类型／目录拒绝，83 项生产打包回归及
实际 Native／ARM 两项检查通过，再以这组最终源码完整重建验证。此前同目录切换测试
fixture 遗留异架构 ELF 也已在测试本身清理，不放宽生产校验。

平台 38 项任务的逐项 implementation／software／physical／commit 状态由 Nexus 的
`docs/implementation/refactor-execution.csv` 和同名 Markdown 维护。本仓库
[重构计划](platform-refactor-plan.md) 保留原任务 ID、预算和 C01–C19 计划批次，
其执行要求不等于所有验收条件已经关闭。

## 验证和移交边界

本轮完整记录见 [最终软件验证](refactor-validation.md) 和
[platform-refactor-validation.json](../evidence/platform-refactor-validation.json)；
[validation.md](validation.md) 与 `evidence/validation.json` 保留首批历史基线。
开发覆盖的定向运行／静态产物仅证明其实际源码；不能沿用旧报告推断新提交通过。
候选证据必须绑定准确双仓 revision、有效配置、Board／layout、工具链与同工件摘要。

所有真实 HIL 当前仍为 **unverified**：本轮没有刷写、读回或执行任何 MCU 镜像。
已准备的准入／租约／超时拒绝工具不能替代 IRQ／AF、UART 最后一位、SPI／DMA、
Flash 物理掉电、栈峰值和长期负载的测量；完整 IRQ／DMA、掉电和长期负载 workload
runner 仍需后续实现与实板验收。
MCU typed I2C 未接入；RS485、网络、受保护 bootloader、MCU crypto／熵源和可信计数器
没有因模型或构建通过而成为已交付能力。

可迁移 bundle 只是某一时刻的 Git 历史快照，需要重新生成并 `git bundle verify` 后
才能用于对应交付；它不包含 SDK 或第三方子模块对象，也不替代远端和依赖身份核验。
默认示例不执行 Flash 擦写、制造注入或真实升级安装。
