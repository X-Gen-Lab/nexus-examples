# 应用层组合片段

这里组合 Nexus 公开接口，不复制平台机制。片段由已有应用执行者调用，不默认创建
任务、引脚连接、存储分区或恢复策略；独立程序与板级装配见 [apps](../apps/) 和
[应用指南](../docs/examples.md)。

| 需求 | 入口 | 应用负责 |
| --- | --- | --- |
| 触摸校准与 ADC → EMA → PID → PWM | [control](control/README.md)、[control_loop](../apps/industrial_control/control_loop.md) | 实测触摸参数、采集周期、系数、I/O owner 与停机 |
| KV／时序记录与 Settings 快照 | [storage](storage/README.md) | 独立区域、schema、deadline、维护授权与恢复 |
| I/O 观测、日志与故障诊断 | [diagnostics](diagnostics/README.md) | 执行上下文、存储、观测事实与故障处理 |
| 更新准备与 USB MSC 交接 | [network](network/README.md) | 认证和版本策略、区域授权、文件系统／USB 独占交接 |

各入口说明实际 C 文件、调用限制与精确测试目标。局部开发反馈不等于所锁定 SDK
的正式集成验收；Native 模型不证明物理行为，实板 HIL 保持 `not_executed`。
