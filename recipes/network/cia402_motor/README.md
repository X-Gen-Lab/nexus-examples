# CiA402 与实际 PMSM 控制链

本软件参考把选定的 Profile Velocity／Profile Position 接到真实的
`example_motor_drive_accept`、Task Axis、快环 Drive／Motor 和 PowerStage 会话。
它使用现有 CAN owner，提供原始命令接收、有限授权、实际反馈和退出的完整组合。
当前独立工程使用 Native 外部端口模型；不建立功率板、电气或实板资格。

```text
CAN value event → CANopenNode／CiA402 → 实际 Axis 准入
                                           ↓
                              有界参考 → IRQ Drive／Motor
                                           ↓
                                  PowerStage 完整输出
                                           ↓
实际传感链 → 连续快环反馈 → Task last_feedback → CiA402／TPDO
```

## 责任与执行者

| 内容 | 本参考的边界 |
| --- | --- |
| CAN | 接收观察器保留实际 RPDO receipt，委托原 owner，不创建第二控制器 |
| Profile | PV 和 PP 使用各自外置对象，PV 不分配 PP 存储 |
| Task | 同一个执行者调用协议、Axis、参数和退出 |
| IRQ | 由既有 PowerStage 执行快环，适配器不访问其 context |
| actual | 只读取 Task 的连续 `last_feedback`、Axis 输出和公共 provider state |
| start | 产品先完成投入运行并显式启动，网络请求从不自动启动硬件 |
| reset | 只报告请求，产品负责原因检查、真实退出和新的运行授权 |

应用选择固定模式。PV 的速度尺度由产品给出；PP 只支持绝对、单点、静止起终点，
没有 SYNC、CSP／CSV、回零或运动中重规划。Profile 的加速、减速、quick-stop
固定值必须与实际 Axis ramp 匹配；PP profile velocity 也须匹配实际轨迹上限。
未实现的不同 ramp 在绑定前返回 `UNSUPPORTED`，不会只改协议对象就声称执行。

## 命令、反馈与期限

七字节 RPDO 的本地接收事件是命令 receipt 来源。接收时间沿用
`nx_can_event_t.timestamp_us`，是控制器获取／报告时间，不能称硬件 SOF。
多帧按所选异步 PDO 的最终完整状态处理；SDO 只改变协议暂存对象。
只有前一步已经完成的 Operational 窗口内的 RPDO 才可授权。收到本机或广播 NMT
立即关闭窗口并丢弃之前尚未准入的候选，完成协议步后才能重新开放。Pre-operational
中被协议丢弃的旧帧，不能被后来的 NMT 或同值 SDO 暂存重新变成运动命令。

每个候选的有效期为原 receipt 加 `command_lifetime_us`，并受独立的
`authority_until` 限制。轮询、心跳、NMT、SDO、重复 action 或 PP held bit 4
都不刷新旧候选的期限。一个实际新 RPDO 即使值相同，也有新的本地接收事实；
这不是端到端鉴权或防重放证明，任何流量都不能延长产品预先授予的整体授权。

PV 参考只有在实际 Axis 接纳后才有 command sequence；快环采用通过连续反馈中
同一 sequence 观察。PP bit 12 等待该实际快环记录，不能把 admission 或目标值
回填成采用事实。bit 10 由真实位置／速度及 dwell 决定，实际 TPDO 不回显 target。
反馈时间表示观察时刻，不代表最早采用时刻或物理 PWM 生效回执。

普通 halt／quick-stop 请求走既有 Axis 停止。传输错误、协议 reset 或真实故障
撤回会话；是否可机械减速仍受母线能量和功率条件约束。长期停机后的新会话必须
由外部重新投入并绑定，本参考不执行自动硬件恢复。
CAN bus-off 等传输关闭会立即撤销 RPDO 准入，按实际功率状态更新本地 PV／PP
故障状态并清除 PP 应答。应用保留首个故障，恢复 CAN 后也需要显式完成退出。
硬件故障、整体授权到期、NMT 撤权及 halt 不等待新的位置样本才处理；缓存观察
仍保留原 sequence 和时间，不通过伪造新样本来推进 PP 状态。

## 配置与使用

独立 `assembly.toml` 明确选择 `components = ["cia402"]`。
产品工程引入 `firmware.cmake`，选择自己的精确 CAN／PowerStage 路线和现有 owner。
按以下顺序使用公共头 `cia402_motor.h`：

1. 取得设备接口，提供实际电机参数、标定和运行预算。
2. 显式初始化并启动 motor；默认 `commissioned=false` 仍拒绝激励。
3. 初始化一个 PV 或 PP profile，提供稳定 node、TX 槽和对齐 workspace。
4. 使用相应 `bind_velocity`／`bind_position`，给出有限产品授权。
5. 在同一个 Task 中调用 `example_cia402_motor_step`；没有真实反馈时返回 `BUSY`。
6. 停止时重复 `example_cia402_motor_stop`，直到协议 TX 和电驱会话均退出。

所有参数、profile、观察器、workspace、motor 与原 CAN owner 均需保持有效，
直到最终退出和外部用户 join。停止失败继续保留原引用，不能清零或换一份 context。
本参考不会停止外部 CAN 控制器，平台最终停机另由应用执行。

## 软件验证

在示例仓库根目录执行，`Nexus_DIR` 指向已准备的源码或 Source SDK 的 `cmake`：

```sh
cmake -S recipes/network/cia402_motor -B build/cia402-motor -G Ninja \
    -DNexus_DIR=/absolute/path/to/nexus/cmake
cmake --build build/cia402-motor --target cia402_motor_contract
ctest --test-dir build/cia402-motor --output-on-failure
```

GoogleTest 使用现有 `scripts/run_google.py` 发现全部用例并校验新鲜非空 JUnit。
它执行真实 CANopenNode、CiA402、Axis、Drive、Motor 和应用 C；只在 CAN／功率
硬件叶端建模。回归覆盖原 receipt、实际采用、目标暂存、reached 资格、失效替代、
PP handshake、reset 及失败排空。独立双 PMSM 动态模型在 control recipe 中维护，
这里不复制另一份电机仿真或测试框架。
