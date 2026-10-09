# 交付与远端同步状态

记录日期：2026-10-09（Asia/Shanghai）。独立示例仓库已创建；本文件区分初始交付、
本轮新 API 实现与仍待收敛的最终证据，不把规划任务或软件结果写成实板资格。

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
正式依赖由 `external/nexus` Gitlink 和 `dependencies/nexus.lock.json` 一起更新、核验；
最终 clean SHA pair、全预设矩阵、源码包和远端新 CI 结果仍待统一记录。
本文件不预填最终 HEAD、测试总数或新 pin 的成功结论。

平台 38 项任务的逐项 implementation／software／physical／commit 状态由 Nexus 的
`docs/implementation/refactor-execution.csv` 和同名 Markdown 维护。本仓库
[重构计划](platform-refactor-plan.md) 保留原任务 ID、预算和 C01–C19 计划批次，
其执行要求不等于所有验收条件已经关闭。

## 验证和移交边界

历史基线及本轮机器记录见 [validation.md](validation.md) 和 `evidence/`。
开发覆盖的定向运行／静态产物仅证明其实际源码；不能沿用旧报告推断新提交通过。
候选证据必须绑定准确双仓 revision、有效配置、Board／layout、工具链与同工件摘要。

所有真实 HIL 当前仍为 **unverified**：板卡刷写／读回、IRQ／AF、UART 最后一位、
SPI／DMA、Flash 物理掉电、栈峰值和长期负载尚不具备本轮合格结论。
MCU typed I2C 未接入；RS485、网络、受保护 bootloader、MCU crypto／熵源和可信计数器
没有因模型或构建通过而成为已交付能力。

可迁移 bundle 只是某一时刻的 Git 历史快照，需要重新生成并 `git bundle verify` 后
才能用于对应交付；它不包含 SDK 或第三方子模块对象，也不替代远端和依赖身份核验。
默认示例不执行 Flash 擦写、制造注入或真实升级安装。
