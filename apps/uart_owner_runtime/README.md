# 多生产者 UART owner / Multi-producer UART owner

本示例覆盖主矩阵：Native 真实线程、FreeRTOS 静态任务、裸机协作循环。两个
producer 各自持有 request、operation、payload 和通知，一个 owner 独占 UART
start/service/cancel；队列容量、优先级、栈、推进周期和恢复策略均由应用选择。

Two producers retain independent storage while one owner services the UART.
Native threads, FreeRTOS tasks and cooperative baremetal progress implement the
same public lifecycle. Notifications are hints; SETTLED remains authoritative.

停止顺序是关闭准入、请求取消、保持 owner 排空、join producer、停止并 join
owner、撤回 IRQ/通知和硬件引用，最后按后端许可停止平台。运行中的 FreeRTOS
保留内核时钟/Tick；producer 退出不等于 scheduler 已停止。BUSY 或失败不能越过
当前回收阶段，QUARANTINED、timeout 和诊断都不释放存储。

Shutdown keeps the owner alive while producers settle and join. Only actual
withdrawal allows notification and application storage reclamation. A running
scheduler retains infrastructure; failed drain retains all loans.

`bootstrap.c` 记录已成功创建资源并区分已运行 Native 线程与尚未启动 scheduler
的静态任务。scheduler 尝试启动后意外返回时，`scheduler_return.c` 保留任务、
IRQ、平台和借用状态，报告故障并进入终止策略，不按“从未运行”回滚。

Bootstrap records successful creations. Unstarted FreeRTOS tasks may be discarded
before scheduler startup; an attempted scheduler start is irreversible for this
policy. An unexpected return retains effects and enters the terminal sink.

`quarantine_policy.c` 的 step/deadline 预算仅控制升级报告。后续 service 仍执行，
直到真实排空；`fault_policy.c` 与 `fault_observer.c` 发布首故障，未绑定或失败的
输出保持 UNPROVEN。产品拥有实际安全输出、复位与恢复决定。

Escalation does not revoke borrowing. The first-fault observer reports software
facts; its output action never proves electrical safety. No automatic reset,
destructive Flash operation or forced thread cancellation is selected.

真实应用 C 路径由 `tests/` 的生命周期、启动失败、隔离、scheduler-return 与故障
合同执行。运行入口见[构建指南](../../docs/build.md)，板级前提见[板卡指南](../../docs/boards.md)。
Tests execute production application paths. All physical timing, stack peaks,
reset and safe-output acceptance remain `not_executed`.
