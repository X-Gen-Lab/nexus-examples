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
| F407 有感电流控制 | [motor_drive](control/motor_drive/README.md) | 校准、初始转子相位、功率板审核及有限运行授权；默认拒绝激励 |
| 双 PMSM 电流／速度／位置 | [dual_motor_drive](control/dual_motor_drive/README.md) | 独立 PMSM 模型、有限参考与共母线预算；Native 软件参考 |
| 双轮差速闭环 | [differential_drive](control/differential_drive/README.md) | 轮径、轮距、完整命令与反馈；当前为独立 DC 模型的 Native SIL |
| 单轴位置／速度／电流 | [servo_axis](control/servo_axis/README.md) | 参数、负载和停止策略；当前为带载 DC 模型的软件闭环 |
| 升降轴与回零 | [lift_axis](control/lift_axis/README.md) | 独立承重、实际闸状态及有限回零；当前只运行机械模型 |
| 有限 CiA 402 PV 主机 | [drive_host](network/drive_host/README.md) | 显式节点、缩放和授权；默认只演示编码，不打开 CAN |
| CiA 402 从站到实际电机链 | [cia402_motor](network/cia402_motor/README.md) | 原始 receipt、有限授权和实际连续反馈；不以协议使能代替 commissioning |

各入口说明实际 C 文件、调用限制与精确测试目标。局部开发反馈不等于所锁定 SDK
的正式集成验收；Native 模型不证明物理行为，实板 HIL 保持 `not_executed`。
