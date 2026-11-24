# 单母线分流软件参考

`single_shunt.cmake` 提供显式选择的 `example_motor_drive_single`，与默认
`example_motor_drive` 使用同一份控制流程，只替换 `current_path.h` 的四个叶实现。
一个镜像选择其中一个库；没有运行时注册、拓扑分派或额外任务。默认相电流路径
不引用单分流重构或 bus planner。

```text
已采用两矢量计划 → 两次原始 DC 电流码 → Current 重构 A/B/C
                                              ↓
                 实际 Drive／Motor → alpha/beta 电压
                                              ↓
                      bus planner → 完整 PWM＋两采样计划
                                              ↓
                    最终量化电压 → dq tracking，完整计划交 provider
```

## 选择与投入条件

选择 `SINGLE_SHUNT`、`phase_samples=1`；provider 必须声明 `dc_link=true`，
相电流 count／mask 为零。DC 电流使用标定槽 0；未安装的其余传感器槽可以全零。
独立母线电压来源与 DC 分流电流不是同一测量。

当前无相移的边沿 PWM 在等 duty 时没有两个独立有效矢量，不能用零电压建立
首次采样。产品必须完成真实投入资格，显式设置 `commissioned=true`、有限启动
预算、非零 `bootstrap_voltage_ab` 和 `bootstrap_bus_voltage_v`。后者是产品提供的
投入阶段母线参考，不能称为本组件实测值。默认均为零，拒绝隐含激励。

首计划按该明确电压产生可观测 PWM。参考 generation 1 的有限投入阶段也使用
该明确电压，并把最终量化电压回送 tracking；实际快环仍检查启动期限。
后续 generation 使用正常控制输出，无窗口时返回 `UNSUPPORTED` 并进入既有退出
流程。不会自动注入最小脉冲、相移或虚构电流；本参考不宣称覆盖任意零速工况。

## 来源、存储与退出

每次捕获必须匹配原计划的 sequence、两次 trigger tick 和开关矢量。电流重构
保留两次观察及最后转换的整个 `scan_span_us`，按 BOUNDED 时序表达有界偏斜，
不把计划 tick 当成实测模拟孔径。产品另行证明该偏斜的电流变化误差可接受。

完整两点 descriptor 位于已声明并借出的 context 内，provider 验证后复制；
不能返回 callback 栈上的 descriptor。一次规划失败不会改写上次 descriptor
或部分输出。既有停止与 `SETTLED` 会合负责释放全部会话借用，禁止提前重用。

软件契约执行实际 Current、bus planner、Drive／Motor、应用 callback 和退出；
仅建模硬件叶端，不据此建立模拟采样精度、硬件时序或功率板资格。
