# 控制与触摸组合

本目录说明应用层组合，不增加设备 provider、默认引脚、任务或恢复策略。

## 电驱与机器人参考

| 入口 | 实际组合 | 软件边界 |
| --- | --- | --- |
| [motor_drive](motor_drive/README.md) | Axis、Drive/Current/Motor/Modulation、强类型 PowerStage、有限参考与连续反馈 | F407 phase 或显式 single 软件路线；默认 commissioning 拒绝激励 |
| [dual_motor_drive](dual_motor_drive/README.md) | 两个完整 PMSM dq／速度／位置链、独立电气机械模型与共母线预算 | Native 平均模型；量化 ADC／编码器是真实控制入口，模型真值只用于评价 |
| [differential_drive](differential_drive/README.md) | 两个 Axis、电流 PI、运动学与有限命令 | Native DC 模型，分别评价轮速、里程计与独立地面真值 |
| [servo_axis](servo_axis/README.md) | 位置／速度／电流闭环、整组参数切换与停止 | Native 带载 DC 模型，采用、轨迹完成和实测到位分别记录 |
| [lift_axis](lift_axis/README.md) | 承重交接、闸状态确认、受控停车与有限回零 | Native 重力与机械闸模型，电流请求不代表实际承重 |

四个 Native 模型入口中的电机数值均由各自示例显式给定，不是平台调参默认。
独立模型只建立选定软件合同；这些参考均未完成实板、机械安全或实时性能验收。
各目录 README 提供独立构建与精确测试入口，公共机制继续链接平台组件。

## 显式触摸校准

`touch_reference.c/.h` 提供 ILI9341／XPT2046 应用层校准片段。调用者提供实测的
原始 X／Y 有效视口两端点、轴交换与反向，以及当前 ILI9341 的 240×320 或
320×240 逻辑尺寸；没有可直接用于实物的默认系数。

映射使用成功采样后复制的 `nx_xpt2046_sample_t`，进行有界整数缩放，先舍入再反向，
视口外返回 EMPTY，不会把越界触点裁剪成有效按钮。输出复制完整的原始 X/Y/Z、
压力存在标志和原始 `observed` 时间戳。映射不读取时钟，也不承诺物理精度、
各轴同时转换或压力已校准。

调用链由同一个外部任务串行推进：成功调用 `nx_xpt2046_sample` 后才将输出交给
`example_touch_map`。只有两步均 SUCCESS 才由应用选择按钮／像素；写入显示屏时，
应用调用 `nx_ili9341_write` 并分别检查返回值与 `written` 前缀。PEN 松开、采样失败
或视口外坐标都不能复用上一次有效触点。

片段没有独立固件 `main`、GPIO owner、后台任务或隐藏帧缓冲，不初始化设备或接管
恢复责任。产品应按实际屏幕和方向测量校准参数；GoogleTest 中的合成数值仅验证算术。

## ADC 流、滤波、PID 与 PWM

实际 C11 组合位于
[`apps/industrial_control/control_loop.c`](../../apps/industrial_control/control_loop.c)，
接口合同见 [control_loop.h](../../apps/industrial_control/control_loop.h)，
完整链路见 [control_loop.md](../../apps/industrial_control/control_loop.md)。它独立于
现有 BMP280/SPI/Modbus 应用；当前 Native Board 没有声明 ADC/PWM 路由，片段不补造。

调用者提供已启动的 ADC/PWM 公开接口、已有 stream、采样周期 `sample_period_us`、
满量程计数、EMA 系数、归一化设定值 `[0,1]`、PID 系数及 `[0,1]` 内的输出范围，
以及已选定的共享 PWM 周期。每个 block 恰好包含一个本机格式的 `uint16_t` 通道结果。
原始计数除以满量程计数只是归一化，不代表电压或传感器校准。PID 使用声明的采集
间隔，不使用任务收件时间或 IRQ 观察时间；产品负责让采集和排队预算满足这个前提。

同一已有执行者串行调用 ADC service、取得 block、EMA、PID、PWM set 和归还借用。
EMPTY 不推进算法状态或重放旧样本；service 返回 BUSY 时仍可能处理已有完成 block。
成功输出复制测量值、命令、占空计数和饱和递增的样本数，不证明电气 PWM 已更新。

loss、错误 block、非法计数、数值错误或 PWM 失败会锁存 FAULT 并请求停止 PWM。
停止失败保留 `output_stop_pending` 与实际结果，供 finish 重试；ADC 停止、生产者
借用和应用退出仍由原 owner 负责，软件结果不证明功率级断能。取得的 block 尽可能在返回前归还；归还
失败时保留原始借用，且 PWM 更新可能已暂存。实际 I/O owner 停止并 join 后，调用
`example_control_loop_finish` 重试遗留归还并进入 STOPPED；应用仍需排空 READY
block、完成 stream 生命周期，再回收存储。片段不包装整套应用停机流程。

完整的有限窗口 ADC source／stream 退出可组合
[stream_recovery 的 ADC 应用](../../apps/stream_recovery/README.md)，由同一已有
执行者逐步推进；它没有默认 Board、采样任务或物理验收声明。
For a complete finite-window source/stream lifecycle, compose the linked ADC
application under the same explicit executor; this control fragment retains its
separate PWM and algorithm responsibility.

## 软件验证入口

完成根工程构建后，精确测试入口为：

```sh
ctest --test-dir build/native-debug \
  -R '^(touch_reference_google|industrial_control_loop_google)$' \
  --output-on-failure --no-tests=error
```

`touch_reference_test` 覆盖视口端点、两种逻辑方向、轴交换／反向、舍入、非法输入和
独立元数据复制。`industrial_control_loop_google` 执行真实 C 组合、公开 I/O 分发、
stream、EMA、PID 和 FSM，仅外部 ADC/PWM provider 操作使用 mock。

未提交的开发 SDK 只用于局部、不可发布的反馈，不能代替所锁定 SDK 的正式集成与
安装消费验收。物理校准精度、ADC 采样孔径、延迟、PWM 时序、对象调参及实板 HIL
均为 `not_executed`。其他器件仍为 `not_selected`，不添加通用空壳或默认替代料号。

## 单轴目标、参数与遥测

[axis_control](../../apps/axis_control/README.md)提供完整的 Thread 模式软件组合：
递增目标序号、epoch 和有效期校验，编码器标定／展开，梯形轨迹、PID、PWM 组更新，
参数整组切换以及明确的保存／恢复。遥测只复制到一个固定 slot，满载覆盖计数可见，
控制不调用网络或持久化。精确功率板、采样拓扑和 IRQ 快环没有从软件参考推定。
