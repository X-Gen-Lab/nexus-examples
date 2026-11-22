# 只读维护诊断组合

本目录的 [maintenance.c](maintenance.c) 是真实 C11 应用组合，调用公开的
`Health`、`Fault`、`Log` 与 `Shell`，复用平台的实际 letter-shell。调用方提供
[maintenance.h](maintenance.h) 中的对象、128 字节行／历史空间、传输回调、
已有日志 sink、版本文本、已捕获的复位位和 retained record。示例没有后台任务、
任务注册表、动态内存、自动喂狗、故障恢复、Flash 写入或默认 reset 操作。

唯一普通任务按以下顺序执行：

1. 正常启动完成后，向 `example_diagnostics_start` 提交稳定的 health facts、
   已锁存的 reset causes、quiescent retained bytes 和有限绝对输出 deadline。
   它先复制 health／故障快照，再初始化 Log 和只读 Shell；损坏的故障记录直接
   报错，不清除或覆盖原记录。此组合不安装异常入口，也不在 fault 中运行 Shell。
2. 每次主循环显式调用 `example_diagnostics_step`，给出最新的同一单调时钟域
   facts、1～16 个字符预算和当前绝对 deadline。它先刷新 health 副本，然后每次
   调用真实 Shell 最多处理一个字节；遇到 EMPTY 即返回。回调同步释放输入／输出
   参数；回调本身也必须有声明的有限运行预算。读取维护命令不自动记作业务进展。
3. 产品先停止并 join 自己的发布者，再调用 `example_diagnostics_stop`：先移除
   Shell 后端引用，再对所选 sink 执行一次 flush，成功后停止 Log 准入。BUSY 或
   失败不释放对象；应用决定何时再次调用 stop。底层共享传输始终由产品拥有。

`health`、`fault`、`reset` 等均为固定只读命令；`reset` 只显示已有原因位。
身份验证、危险维护命令授权、重启、crash 持久化区域、ack 与重放策略属于产品。
零 findings 不证明系统健康。`retained` 未初始化、保留区 linker 规则及目标复位域
必须由真实产品审查；本 Native 示例用普通 RAM 验证软件行为，不声称物理保留。

## 选择传输

测试使用外部有限 byte transport，同时作为 Shell IO 和已存在的
`nx_log_sink_port_t`，真实组件不被 mock。生产可选既有 UART settled adapter
或已选定的 RTT sink。选择 RTT 时显式调用 `nexus_add_rtt()` 并链接 `Nexus::RTT`；
精确 Shell 存储仅由组合 leaf 链接 `Nexus::ShellStorage` 获取，不向普通业务传播。

RTT 装配由产品完成：对象 `nx_rtt_t`、up/down rings 和 channel name 全部使用
静态 image-lifetime 存储；在渠道 1～3 中选一个未绑定渠道，调用 `nx_rtt_init`，
将 `nx_rtt_log_sink(&rtt)` 交给本组合。Shell 的 read 回调直接调用
`nx_rtt_read`；write 回调用同一 sink 的 write、`NX_LOG_INFO` 和原始 deadline。
此适配只连接已有公开接口，不另建缓冲区、异步队列或执行者。应选足以接受初始提示
与单条记录的 up ring；RTT SKIP 在容量不足时整体拒绝记录，由产品决定退出或丢弃。

退出顺序仍为停止发布者、退出此组合、再由传输 owner 调用 `nx_rtt_stop`。
RTT stop／flush 可能因 host 尚未读取返回 BUSY，不等待 host，也不自动 retry。
**即使 stop 成功，RTT control block 仍引用 rings 和 channel name，这些存储必须
保留整个 image，不能因本组合退出而释放或重新绑定。** UART 的异步 request 则
必须继续推进到 acquire-observed SETTLED，并 join publisher 后才可回收其存储。

## 契约执行与范围

`tests/apps/recipes/diagnostics_reference.cmake` 注册真实 C 应用的四个 Google
契约：health／fault 只读输出和字符预算、无效预算无 I/O、prompt／read 错误退出，
以及 flush BUSY 时保留 sink 生命周期并由调用方显式重试。使用仓库已锁定 SDK：

```sh
python scripts/build.py --preset native-debug --parallel 1
ctest --test-dir build/native-debug -R '^diagnostics_reference_google$' \
  --output-on-failure --no-tests=error
```

正式集成由 `external/nexus` 的 Gitlink 与 `dependencies/nexus.lock.json` 共同
锁定平台源码；安装 SDK 消费核对相同 commit／tree 和包身份。完整 Native 执行
验证软件组合行为；ARM 链接资源、物理时序、RTT host 负载、
异常入口、断电和实际复位保留均需独立验证，本站物理状态为 `not_executed`。

已有 [stream recovery 应用](../../apps/stream_recovery/README.md) 继续展示
`nx_io_diagnose` 的 request／stream／lifecycle 事实与 LOSS／BUSY stop，它不被本
维护入口替代。本组合仅消费由业务 owner 已复制、稳定的诊断事实。

## 故障持久化与分片导出

[fault_export.c](fault_export.c) 补齐正常启动后的消费链：异常入口仅调用已有固定
capture；Storage 就绪、产品允许维护写入后，由普通任务调用
`example_fault_export_begin`。它调用真实 `nx_fault_record_persist`，先 durable save，
再 acknowledge retained capture。失败保留 retained 数据；含糊的失败提交要求按
Storage 合同 reopen，不能盲目重放。没有新 retained capture 时可重新导出已保存记录。

begin 检查完整 160 字节、CRC／版本和调用方明确选择的 16 字节 ELF build ID，然后
形成调用者对象中的固定快照。错误 ID 不清除 retained 或 persistent 数据。导出
不安装 fault handler、不初始化介质，也不选择 Flash region、自动格式化或擦除。

每次 `example_fault_export_step` 最多复制调用方指定的 1～160 字节，共用传入的有限
绝对 deadline。sink 返回精确 copied prefix，任何返回都结束源借用；BUSY、满载和
错误保留快照与位置，不忙等。offset 仅表示本地同步 sink 已接纳，未证明 host 接收
或持久保存。导出完成仍保留 persistent record，重启允许再次导出；序号和 build ID
用于业务去重，不将本地 flush 当作远端确认。

接收端在公开资料允许的范围内保存准确的 160 raw bytes，然后使用对应 SDK 的工具：

```sh
python external/nexus/tools/dev/fault_decode.py fault.bin \
  --elf exact-firmware.elf --addr2line arm-none-eabi-addr2line
```

decoder 检查 captured build ID 与实际 ELF 常量，错误身份不能解析为可信符号。
代码、凭据、私有 PCB 和故障内存资料应按所属产品仓库的公开边界处理。

`terminal_reference_google` 使用真实两 bank Storage 与 Fault C，实现 save-before-ack、
损坏／身份拒绝、断续导出、满载和重启后重读。RAM 介质模型不建立物理掉电资格。

## 调参与诊断入口

现有 letter-shell profile 保持固定只读命令表。参数写入由产品的有限类型化命令入口
完成范围、版本、有效期和授权检查，再交给控制参数快照；不能直接把 live PID 字段
暴露为可写 Shell 变量。RTT／UART 只提供所选有界传输。多个协议共享物理通道时，
应用提供一个明确 owner 和 framing／分流，不能让两个 parser 争抢同一 RX 字节流。
