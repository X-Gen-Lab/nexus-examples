# 有限 UART workload / Finite UART workload

本目录只维护 `workload.c/.h` 应用片段，由 Native Google 合同编译真实 C 路径。
没有独立 `main`、CMake 工程或实板采集入口，不再声明 H743/U575 工程、站点 arming
或已退出的 preset。调用者提供已经启动的公开 byte-UART 和持久静态状态。

This tested application fragment has no standalone firmware entry. The caller
supplies an already started public byte-UART, a finite session and persistent
storage. Host contract execution does not operate a station or qualify FIFO/DMA
hardware timing.

单个未屏蔽 task 拥有状态，每步至多一次 service、一次有界 RX read 和一次 TX
submit；TX 被借用时仍可接收。每个被接受的 32 字节 TX 带 session/sequence，
只有 acquire 观察 SETTLED 并消费结果后才准备下一个 payload。OVERFLOW 保留有效
RX 前缀与诊断；其他致命错误关闭准入，但不释放仍被借用的 request/bytes。

One unmasked task steps the workload. RX can progress during an active TX loan.
Accepted TX storage remains unchanged through settlement; overflow and other
errors retain their actual prefix and ownership facts.

窗口结束、cancel 和 timeout 只进入排空。排空到期保持 RETAINED，允许晚到的
完成在后续 step 结清；时钟 epoch 非法则保留状态，不再用错误时间调用 provider。
只有真实 UART stop 后才 CLOSED，再由调用者 join 其他使用者并停止 Platform。

Closing admission never revokes an accepted borrow. Drain expiry retains static
storage; invalid clock ordering prevents further provider access. CLOSED proves
UART stop only, not full application or platform reclamation.

`fifo_acquisition_join_platform` 最多调用显式 stop 回调 64 次，不在清理开始后读取
可能已被撤回的时钟。次数上限不是任意外部回调的 WCET 或整体停机时限；失败仍须
保留静态状态并交由产品恢复。运行入口是[完整应用合同](../../docs/validation.md)。

The finite cleanup-attempt budget does not bound arbitrary callback duration.
Failed cleanup retains storage. All physical timing and board behavior remain
`not_executed`.
