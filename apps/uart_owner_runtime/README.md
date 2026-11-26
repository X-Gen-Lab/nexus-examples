# 多生产者 UART owner / Multi-producer UART owner

本示例覆盖主矩阵：Native 真实线程、FreeRTOS 静态任务、裸机协作循环；另有显式
ThreadX 软件参考。四条路径编译同一份业务 C。两个
producer 各自持有 request、operation、payload 和通知，一个 owner 独占 UART
start/service/cancel；队列容量、优先级、栈、推进周期和恢复策略均由应用选择。

Two producers retain independent storage while one owner services the UART.
Native threads, FreeRTOS/ThreadX tasks and cooperative baremetal progress compile
the same business C and implement the public lifecycle. Notifications are hints;
SETTLED remains authoritative.

停止顺序是关闭准入、请求取消、保持 owner 排空、join producer、停止并 join
owner、撤回 IRQ/通知和硬件引用，最后按后端许可停止平台。运行中的 FreeRTOS／ThreadX
保留内核时钟/Tick；producer 退出不等于 scheduler 已停止。BUSY 或失败不能越过
当前回收阶段，QUARANTINED、timeout 和诊断都不释放存储。

Shutdown keeps the owner alive while producers settle and join. Only actual
withdrawal allows notification and application storage reclamation. A running
scheduler retains infrastructure; failed drain retains all loans.

请求结果异常或未取得时，完成回收后仍按后端策略尝试停止平台。停止失败优先报告
实际清理错误。允许停止的平台成功退出后，再报告请求失败。

A request or result observation failure still permits eligible Platform stop
after reclamation.
The application reports a Platform stop error first. After a successful stop it
reports the request failure.

owner join 后，协调者先撤回 IRQ wake。若 UART stop 返回 `BUSY`，协调者在以下
任一预算耗尽前推进并重试：100 次 service 或单调时钟的 100 ms。随后仍未停止则保留
通知与平台；已撤回的 wake 不重复撤回。重新尝试前由产品确认恢复条件。

After the owner joins, the coordinator detaches the IRQ wake and services a
UART stop that returns `BUSY`. It retries the incomplete stop for up to 100
service passes within a 100 ms monotonic budget. Reclamation starts only when
stop succeeds. An exhausted budget retains the notifications and platform;
the product owns any further recovery.

`bootstrap.c` 记录已创建的通知、内核任务启动候选和已运行的 Native 线程。即使任务
启动返回错误，也先证明候选已撤回，再销毁通知或平台。scheduler 尝试启动后意外
返回时，`scheduler_return.c` 保留任务、
IRQ、平台和借用状态，报告故障并进入终止策略，不按“从未运行”回滚。

Bootstrap records successful notifications and every kernel task-start candidate.
A failed start can retain a task; only successful discard authorizes subsequent
notification or Platform cleanup. An unproved discard enters the retained terminal
policy, including `INVALID`, which is not treated as proof of absent resources.
Unstarted kernel tasks may be discarded
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

## ThreadX：独立编译与真实内核 / Explicit ThreadX paths

业务的 producer、owner、取消、排空和 join 共用 `main.c`。应用装配只选择通知／栈
存储、启动边界与终止策略；普通 I/O 不经过另一层 OS 转发。`threadx.toml` 明确
选择启明 V3.1、USART1、ThreadX 和可调用内核的 IRQ 优先级，不改变九个主 preset。
任务控制与 live 标志单独存放，不嵌入被任务借用的业务 context；栈也独立保留。
从仓库根目录准备固定 SDK 与 ARM 工具链后执行：

```sh
cmake -S apps/uart_owner_runtime -B build/uart-owner-threadx -G Ninja \
  -DNEXUS_SOURCE="$PWD/external/nexus" \
  -DNexus_DIR="$PWD/external/nexus/cmake" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-gcc.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/uart-owner-threadx --target uart_owner_runtime --parallel 2
```

独立 Linux 软件合同执行真实 ThreadX 内核、同一业务 C 和公开 Native UART 模型，
检查三个实际 worker join/delete、IRQ 发布源撤回、一次 UART stop BUSY 后的实际
service 与重试、四个通知销毁及 Tick 继续运行。
同一入口还执行通知／任务部分创建失败的逆序回滚测试。需要 Linux GNU 端口、
宿主 GoogleTest 依赖和显式 host-test support：

```sh
cmake -S tests/apps/uart_owner_runtime/threadx \
  -B build/uart-owner-threadx-host -G Ninja \
  -DNexus_DIR="$PWD/external/nexus/cmake" -DNEXUS_TEST_SUPPORT=ON
cmake --build build/uart-owner-threadx-host --parallel 2
ctest --test-dir build/uart-owner-threadx-host \
  --output-on-failure --no-tests=error
```

The standalone host contract runs the real Linux kernel and production application
with an explicitly selected UART model. Bootstrap failures mock external ports;
the lifecycle injects one stop `BUSY` and executes the real provider on retry
after task joins/deletions and before notification destruction. The
coordinator control remains retained after entry return, and a live kernel keeps
its Platform clock/Tick. Neither host execution nor the ARM link proves MCU timing,
physical IRQ priorities, stack peaks or electrical output behavior.
