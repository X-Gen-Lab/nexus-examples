# 有限 CiA 402 PV 主机参考

本目录是实际可执行的 Python 3.11+ 标准库主机应用，通过 Linux SocketCAN 使用
Nexus 当前选定的异步 Profile Velocity 映射。它不向 MCU 引入 Python、ROS 2、
主机线程或协议对象，不选择功率板、电机、方向和控制参数。

默认仅运行 codec 演示，不打开接口、不发送 NMT、不使能驱动：

```sh
python recipes/network/drive_host/host.py
```

输出明确标记 `codec_demo_no_hardware`。演示中 node=18、0.01 rad/s/LSB 和
最大 raw=1000 是软件数值，不是 CiA 402 的固定单位或产品默认。

## 精确消息合同

本配方对应所选 SDK 的
[`cia402.h`](../../../external/nexus/components/protocols/canopen/include/nexus/components/cia402.h)
和真实 CANopenNode 对象映射。驱动应用已经配置相同映射和周期 TPDO 才能使用。

| 对象 | CAN ID／顺序 | 格式 |
| --- | --- | --- |
| NMT start | ID 0；command=1、node ID | 两字节 |
| RPDO | `0x200 + node`；6040 controlword、6060 mode、60FF velocity | 小端 `<Hbi`，七字节 |
| TPDO | `0x180 + node`；6041 statusword、6061 actual mode、606C velocity | 小端 `<Hbi`，七字节 |

mode 为有符号 int8，PV=3；速度为有符号 int32。主机配置
`velocity_unit_rad_s`，且必须与驱动的机械侧、方向和单位完全一致。数值采用最近舍入，
半数远离零；报告保留原请求与量化后的 wire 速度，不隐瞒编码差异。

状态按 `statusword & 0x006f` 解释；电压、remote 和其他状态位不混入 PDS 判定。
本映射不包含命令序号、执行时间或应用层采用回执。主机内部时间和身份不会被伪装成
标准 PDO 字段，也不适用于位置模式或其他厂商的私有 CAN 协议。

## 有限执行流程

```text
验证原始有限授权与所有参数
  → NMT Operational 请求
  → CW6／零速，等待实际 Ready 状态和 mode=3
  → CW7／零速，等待实际 Switched on 状态和 mode=3
  → CW0xF／零速，等待实际 Operation enabled 状态和 mode=3
  → 在原授权期内发送有限速度请求，并监督驱动回报
  → CW0xB／零速，请求 Quick Stop
  → 在独立清理预算内观察 Quick Stop 状态及零速回报
```

任何一步的超时、bus 错误、模式丢失或反馈异常都会结束普通发送，并尝试有界清理。
清理失败作为 `stop_error` 保留，不以丢弃错误或刷新原 deadline 掩盖。这里不自动
Fault Reset、不在失败后重新使能，也不执行硬件复位。

`run_velocity(port, request, config, monotonic=...)` 使用显式 CAN 端口：

- `send(frame, deadline)`：在有限本地单调 deadline 内发送完整帧。
- `receive(deadline)`：返回 `Frame`，或在 deadline 到达时返回 `None`。
- port 的关闭与其他使用者退出由调用者负责。

`VelocityRequest` 保存原始 `issued_at` 和 `valid_until`。握手、重复发送和接收
反馈都不会延长它；相同速度的重复帧只属于这一次有限授权。生产系统的规划任务必须
独立证明新目标来源，不能由通信线程给冻结的数据反复换号。

该授权只存在于主机。选定标准 PDO 没有携带本地 lease；本配方不能替代 MCU 的命令
监督、限位、反馈检查和故障反应。主机断电或链路中断后，Quick Stop 帧可能无法送达。

## 显式 SocketCAN 使用

只有指定 `--interface` 才打开 Linux CAN socket。此时 node、单位、最大 raw、速度
和持续时间均须显式提供；模块导入和默认演示没有硬件副作用。

接入前核对实际驱动配置、功率板、机械条件、供电和回馈能力。以下仅展示完整语法，
当前未在实物接口执行：

```sh
python recipes/network/drive_host/host.py \
  --interface can0 --node 18 --unit-rad-s 0.01 --maximum-raw 1000 \
  --velocity-rad-s 1.0 --duration-s 1.0
```

SocketCAN 端口采用经典标准数据帧、目标 TPDO 过滤和 CAN controller error-frame
接收。socket 为非阻塞模式，读写使用原绝对预算。程序不创建接口，不改变 bitrate，
也不自动调整 NMT、PDO 映射或驱动器保护参数。

## 报告如何解释

| 字段 | 精确含义 |
| --- | --- |
| `enabled_confirmed` | 在主机观察到实际 PDS enabled 与 mode=3 回报 |
| `velocity_frames_sent` | 完整速度帧被端口接受发送的次数，不是运动完成次数 |
| `reference_adoption` | 始终为 `not_reported_by_profile`，没有伪造采用序号 |
| `quick_stop_sent` | 主机端口接受了 Quick Stop 请求 |
| `quick_stop_state_reported` | 观察到驱动回报 Quick Stop Active 与 mode=3 |
| `zero_velocity_dwell_reported` | 有界间隔的连续回报满足所选零速窗口 |
| `physical_stop_verified` | 始终为 false，没有独立物理停止证据 |

TPDO 不带速度样本序号或采样时刻。`observed_at` 是本地主机读到帧的观察时间，
不是总线 SOF、驱动实际采样时间或参考采用时间。即使没有丢包，也不能据此证明传感器
始终更新、机构已经停稳、抱闸已建立或功率级已经断能。反馈缺失不能填成零速，稀疏
的两份零速回报也不能直接拼成连续驻留。

退出码 0 只表示这次主机协议流程和回报观察完成；软件结果不授予实板资格。

## 验证与 ROS 2 边界

```sh
python -m unittest discover -s tests/apps/drive_host -p test_drive_host.py -v
```

测试执行真实 codec 和有限控制流程，仅 CAN 端口与单调时钟由外部 fixture 提供。
覆盖负数、缩放、状态／模式确认、原授权过期、迟到、bus 错误、稀疏反馈和停止未确认。
SocketCAN 边界测试直接核对 Linux 帧与过滤器，并验证等待迟返后不会发送过期请求、
接口绑定失败会关闭 socket；这些测试使用注入的 socket，没有创建实际 CAN 接口。
当前真实 CAN 总线、控制对象和物理时序均为 `not_executed`。

ROS 2 主机可以在自己的硬件适配中连接 wheel velocity/state 接口；车体到轮速的
转换只放在一侧。当前 recipe 是单次有限 PV 会话，不是已经验证的 `ros2_control`
插件，不能在每个 ROS 更新周期重新执行整套握手。连续 ROS 集成需要另行实现并验证
持久会话、新目标授权与失联处理。本目录不添加未运行的 rclpy 占位程序。
