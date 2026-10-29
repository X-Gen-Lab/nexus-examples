# U575 finite SRAM4 copy across Stop2 / Stop2 中的有限 SRAM4 复制

This standalone C11 application selects exact STM32U575ZIT6Q, TZEN0, baremetal,
MSIS4 and the optional nonsecure SRAM4 region. It links only
`Nexus::U575Stop2Copy`, constructs the public retained-clock and LPDMA1/channel0
owners, and runs one finite 17-byte memory-to-memory copy. No peripheral route,
general LPBAM scheduler, RTOS worker or factory RTC/AlarmA owner is selected.

本独立 C11 工程选择精确 STM32U575ZIT6Q、TZEN0、裸机、MSIS4 及可选非安全
SRAM4 区域。它只链接 `Nexus::U575Stop2Copy`，构造公开 retained-clock 与
LPDMA1/channel0 owner，执行一次有限的 17 字节内存复制。不选择外设路由、通用
LPBAM 调度器、RTOS worker 或 factory RTC／AlarmA owner。

## Build / 构建

Use the clean SDK selected by this repository's Gitlink and source lock. The
authored `assembly.toml` belongs to this recipe; it does not add a maintained
preset or replace the SDK's canonical assembly. Only public headers and targets
are consumed. The WUT-only `Nexus::U575Stop2` target is an alternative and must
not be linked alongside the copy owner.

使用本仓库 Gitlink 与源码锁选定的干净 SDK。本工程拥有独立 `assembly.toml`，
不增加 maintained preset，也不替换 SDK 规范 assembly。只使用公开头文件与
target。仅 WUT 的 `Nexus::U575Stop2` 属于另一种选择，不得与 copy owner 同时链接。

```sh
cmake -S apps/retained_stop2/u575_copy -B build/retained-stop2-copy -G Ninja \
  -DNEXUS_SOURCE="$NEXUS_SOURCE" \
  -DCMAKE_TOOLCHAIN_FILE="$NEXUS_SOURCE/cmake/toolchains/arm-gcc.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/retained-stop2-copy --target retained_stop2_copy --parallel 1
```

The assembly reserves a 2048-byte MSP, zero libc heap, a 4096-byte primary
SRAM1 reservation ceiling, a 65536-byte Flash-load ceiling and a separate 512-byte SRAM4
ceiling. The link/resource report establishes actual use; these limits establish
neither worst-case stack depth nor physical retention.

assembly 预留 2048 字节 MSP、零 libc heap，设置 4096 字节主 SRAM1 预留上限、
65536 字节 Flash load 上限及独立 512 字节 SRAM4 上限。实际用量由链接／resource
报告确定；这些上限不证明最坏栈深度或物理保留。

## Prepared boot and opt-in / Boot 准备与显式启用

Execution defaults to returning before clock construction or Platform start.
After reviewing the complete public
[copy contract](../../../external/nexus/docs/design/u575-stop2-copy.md) and
[retained-time/Stop2 contract](../../../external/nexus/docs/design/u575-retained-time.md),
a reviewed hardware run may halt at `main` and set
`example_stop2_copy_authorization=0x4e584350`. The switch is initially zero and
is an example opt-in, not security authentication. This recipe does not prepare
an RTC calendar, select an oscillator or establish board electrical facts.
Selected reset startup still validates/enables SRAM4/LPDMA gates and initializes
SRAM4 DATA/BSS before `main`, even when authorization remains zero. Prepared
reset supply/retention premises therefore apply before this runtime opt-in.

默认执行在构造时钟或启动 Platform 前返回。完整审查公开
[copy 合同](../../../external/nexus/docs/design/u575-stop2-copy.md) 与
[retained-time／Stop2 合同](../../../external/nexus/docs/design/u575-retained-time.md)
后，经过评审的实物运行可在 `main` 断点设置
`example_stop2_copy_authorization=0x4e584350`。开关初值为零，只是示例显式启用，
不是安全认证。本工程不准备 RTC 日历、不选择振荡器，也不证明板级电气事实。
即使 authorization 保持为零，所选 reset startup 仍在 `main` 前验证／开启
SRAM4／LPDMA gate 并初始化 SRAM4 DATA／BSS，因此 reset 的供电／保留前提必须
在运行时开关之前满足。

External boot supplies the exclusive valid raw BCD24h RTC calendar, LSE32768
crystal mode (`LSEBYP=0`), conservative drive 10/11, PRER127/255, BYPSHAD,
RTCEN/RTCAPBEN and DBP=1. AlarmA registers, WUT, flags and RTC IRQ start quiet;
there is no other RTC writer or calendar reader. Board/boot owners review the
exact silicon revision and ES0499 errata and exclude PC13 transitions.

外部 boot 提供独占、有效的原始 BCD24h RTC 日历、LSE32768 晶体模式
（`LSEBYP=0`）、保守 drive 10/11、PRER127/255、BYPSHAD、RTCEN／RTCAPBEN 及
DBP=1。AlarmA 寄存器、WUT、flag 与 RTC IRQ 初始静止；没有其他 RTC writer 或
日历 reader。Board／boot owner 审查精确硅片版本与 ES0499 勘误并排除 PC13 跳变。

Selected reset startup requires DMA gates initially off, verifies the existing
Range4 supply and SRAM4 power/retention state, enables SRAM4, initializes its
DATA/BSS, then enables and reads back LPDMA1EN. It preserves NOINIT bytes.
Runtime must keep SRAM4EN and LPDMA1EN enabled and configuration stable through
all stops and joins. All four LPDMA channels/vectors initially are quiet; no
other LPDMA channel may start. Runtime never repairs SRAM4 by changing its
power-down fields.

所选 reset startup 要求 DMA gate 初始关闭，验证既有 Range4 供电与 SRAM4
电源／保留状态，开启 SRAM4、初始化其 DATA／BSS，然后开启并读回 LPDMA1EN。
NOINIT 字节保持原样。运行时必须在所有 stop／join 完成前维持 SRAM4EN、
LPDMA1EN 与配置稳定。全部四路 LPDMA channel／vector 初始静止，不得启动其他
channel。运行时不会通过修改 power-down 字段来修复 SRAM4。

Require TZEN0, unswapped Flash, effective MSIS4/MSIK4, equal buses and true
Range4 (`VOS=00`, `VOSRDY=1`, `BOOSTEN=0`). The caller is privileged MSP Thread,
CONTROL=0; no scheduler has ever started. Entry and every recovery require
PRIMASK=1 and BASEPRI=FAULTMASK=0. All SRAM remains retained. Caches, debug,
MPU, SysTick, other IRQ/EXTI sources and peripherals must meet the exact quiet
profile. Exclude watchdogs, other bus masters, debugger, NMI/HardFault activity
and asynchronous configuration writers externally. Compilation and quiet
register values cannot prove these external obligations.
Clock/copy initialization and retained-clock stop require unmasked privileged
Thread (`PRIMASK=0`). A debugger used to set the opt-in must satisfy the debug-off
entry premises before the transition executes.

要求 TZEN0、未交换 Flash、有效 MSIS4／MSIK4、相等总线频率及真实 Range4
（`VOS=00`、`VOSRDY=1`、`BOOSTEN=0`）。调用者为特权 MSP Thread、CONTROL=0，
调度器从未启动。入口与每次恢复均要求 PRIMASK=1、BASEPRI=FAULTMASK=0。所有
SRAM 保留。cache、debug、MPU、SysTick、其他 IRQ／EXTI 源及外设满足精确静止
配置。外部排除 watchdog、其他 bus master、调试器、NMI／HardFault 活动和异步
配置 writer。编译通过或寄存器静止值不能证明这些外部义务。
clock／copy 初始化与 retained-clock stop 要求未屏蔽特权 Thread
（`PRIMASK=0`）。用于设置开关的调试器必须在转换执行前满足 debug-off 入口前提。

## Memory and one cycle / 内存与一次闭环

The 17 initialized source bytes use SRAM4 DATA, the separate 17 destination
bytes use SRAM4 BSS, and the four-byte-aligned 24-byte workspace uses SRAM4
NOINIT. The complete spans are disjoint within `0x28000000..0x28003fff`; padding
gives 64 bytes of selected SRAM4 storage. NOINIT does not mean a valid descriptor:
the provider overwrites all six LLI words only after admission. CPU clock/copy
owners, request, saved mask, barrier and lease remain separate static SRAM1
control storage. Do not mutate any borrowed metadata, source or workspace or
read destination while the loan is live.

17 个初始化 source 字节位于 SRAM4 DATA，独立 17 字节 destination 位于 SRAM4
BSS，四字节对齐的 24 字节 workspace 位于 SRAM4 NOINIT。完整 span 在
`0x28000000..0x28003fff` 内互不重叠；计入对齐后共占 64 字节。NOINIT 不表示
descriptor 有效：provider 仅在接纳后写入全部六个 LLI word。CPU clock／copy
owner、request、原始 mask、barrier 与 lease 位于独立静态 SRAM1 control storage。
借用期间不得修改 metadata、source 或 workspace，也不得读取 destination。

The application uses the explicit AlarmA-copy clock initializer and binds both
owners to the same concrete Stop2 identity. It initializes the request, takes
a separate successful retained sample, and prepares `NX_DEADLINE_NEVER` before
RUN shutdown. No Core TIM2 deadline crosses stop or is converted to the retained
domain. The retained clock has a 60-second maximum sample gap, which must cover
the entire actual arm, sleep, wake and drain interval.

应用使用专用 AlarmA-copy clock initializer，把两个 owner 绑定到同一具体
Stop2 identity。RUN 关闭前初始化 request、获取独立成功 retained sample，并用
`NX_DEADLINE_NEVER` 准备请求。Core TIM2 deadline 不跨越 stop，也不转换到保留
时间域。retained clock 的最大采样间隔为 60 秒，必须覆盖实际完整 arm、sleep、
wake 与 drain 时间。

The empty selected Platform is enrolled as one explicit RUN consumer. The
shared application helper closes admission, actually stops Platform, releases
the exact lease only after drain and proves the closed barrier quiescent.
It saves/disables the incoming mask and invokes the copy entry once. AlarmA's
next whole-second event triggers the first 8 incrementing bytes on rising
trigger 14; the generated second LLI copies the remaining 9 bytes without a
trigger. The final link is zero. WUT supplies an independent escape at 4096
RTC/16 ticks, nominally 2 seconds at 2048 Hz. AlarmA has all calendar masks set,
ALRMASSR=0, ALRAIE=0 and ALRAFCLR=0.

所选空 Platform 作为一个显式 RUN consumer 注册。共享 application helper 关闭
admission、真正停止 Platform，仅在 drain 后释放精确 lease，并验证关闭 barrier
已 quiescent。随后保存／屏蔽原始 mask，仅调用一次 copy entry。AlarmA 的下一个
整秒事件通过 rising trigger 14 触发前 8 个递增字节；生成的第二个 LLI 无需
trigger，复制剩余 9 字节，最终 link 为零。WUT 以 4096 个 RTC/16 tick 提供独立
escape，在 2048 Hz 下标称 2 秒。AlarmA 设置全部日历 mask，ALRMASSR=0、
ALRAIE=0、ALRAFCLR=0。

RTC IRQ2 or channel0 IRQ114 may wake masked WFI without running an ISR. Pending
wake can return immediately. `wait_executed` proves only WFI execution.
WUT escape is a transfer timeout, not successful copy completion. Retained
snapshots resolve 3906.25 microseconds. The 10000-observation per-phase poll
budget and 64 application progress attempts do not bound wall time; one WFI
may never return if physical wake fails.

RTC IRQ2 或 channel0 IRQ114 可在屏蔽状态唤醒 WFI，无需执行 ISR；pending wake
可使其立即返回。`wait_executed` 仅证明执行 WFI。WUT escape 表示传输超时，
不是复制成功。retained 快照分辨率为 3906.25 微秒。每阶段 10000 次观察预算及
64 次 application progress 尝试都不限制墙钟时间；物理唤醒失效时 WFI 可永不返回。

## Failure and storage ownership / 失败与存储所有权

Rejection retains no new loan and the READY request needs no settlement.
`accepted=true` borrows the request, both buffers and workspace even if later
setup fails. Keep the exact closed barrier and every owner alive. The helper
retries only `recover`, never another WFI, while retaining PRIMASK. DMA idle,
cancel, timeout, or a finite retry count cannot release this compound loan.
Only after RTC producer withdrawal, DMA final-access proof, vector/gate/clock
restoration and final release does the provider publish SETTLED. A final-release
failure retains the copy marker even when hardware effects are already zero.

拒绝不保留新借用，READY request 无需 settlement。`accepted=true` 会借用
request、两个 buffer 和 workspace，即使稍后 setup 失败也如此。必须保留精确
关闭 barrier 与所有 owner。helper 在保留 PRIMASK 时只重试 `recover`，不再次
WFI。DMA idle、cancel、timeout 或有限重试次数都不能释放此复合借用。只有 RTC
producer 撤销、DMA final-access 证明、vector／gate／clock 恢复及 final release
全部完成后，provider 才发布 SETTLED。即使硬件 effects 已为零，final-release
失败仍保留 copy marker。

Before mask restoration or reopening RUN, the join hook acquire-observes the
accepted request's settled result, requires success, exactly 17 transferred
bytes and byte equality, then stops the copy owner. Request failure suppresses
restart even after successful recovery. Original entry observations are saved
separately from later cleanup results. Only a successful drained operation
reopens RUN, acquires a fresh lease, starts a new Platform/Core epoch and stops
and releases it again. Initial/restarted partial Platform startup also requires
actual stop before its lease is released.

恢复 mask 或重开 RUN 前，join hook 以 acquire 观察已接纳 request 的 settled
结果，要求成功、精确传输 17 字节且逐字节相等，然后停止 copy owner。即使恢复
成功，request 失败仍禁止 restart。原始 entry 观察值独立保存，不被后续 cleanup
结果覆盖。只有成功且完全排空的操作才重开 RUN、获取新 lease、启动新
Platform／Core epoch，随后再次 stop／release。初次或重启时的部分 Platform
启动失败，也须真正 stop 后才释放其 lease。

Unknown RUN access profile permits only safe CPU/vector withdrawal and retains
peripheral effects; an external reviewed owner must restore the exact profile.
Observed external LPDMA reset during an active loan permanently quarantines the
loan. Reset deassertion or quiet reset values cannot prove final access; only an
actual external system reset ends that responsibility. The recipe never forces
release, changes power policy or resets equipment.

未知 RUN 访问配置只允许安全 CPU／vector 撤销并保留外设 effects，须由经过评审
的外部 owner 恢复精确配置。活动借用期间观察到外部 LPDMA reset 会永久隔离该
借用。解除 reset 或静止 reset 值不能证明 final access，只有实际外部 system
reset 才结束责任。本工程不强制释放、不改变电源策略，也不重置设备。

A failed stop, join or unfinished phase parks with all static storage, leases,
barrier and any retained mask intact. A drained failed operation keeps RUN
closed. Only after every copy/wake/Platform/clock user has joined does explicit
retained-clock stop permit return; failed clock stop also parks. A tainted
retained epoch may settle an error but cannot publish successful elapsed time.

stop、join 失败或 phase 未完成时，原地保留全部静态存储、lease、barrier 与仍持有
的 mask。完全排空的失败操作保持 RUN 关闭。只有 copy／wake／Platform／clock
用户全部 join 后，显式 retained-clock stop 成功才可返回；clock stop 失败也
原地保留。被污染的 retained epoch 可以 settle 错误，但不能发布成功 elapsed。

Observe `example_stop2_copy_result`, `example_stop2_copy_phase`,
`example_stop2_copy_accepted`, `example_stop2_copy_dma_wake`,
`example_stop2_copy_settled_result`, `example_stop2_copy_transferred`,
`example_stop2_copy_restarted` and `example_stop2_copy_elapsed_us`.
Elapsed is published only from successful, same-identity/generation ordered
retained snapshots after all joins.

可观察 `example_stop2_copy_result`、`example_stop2_copy_phase`、
`example_stop2_copy_accepted`、`example_stop2_copy_dma_wake`、
`example_stop2_copy_settled_result`、`example_stop2_copy_transferred`、
`example_stop2_copy_restarted` 与 `example_stop2_copy_elapsed_us`。elapsed 仅在
全部 join 后，从成功、同 identity／generation 且有序的 retained 快照发布。

## Verification boundary / 验证边界

Native tests exercise application policy at public SDK boundaries; the focused
ARM build checks this standalone C11 image and authored memory budgets. Neither
establishes physical copy correctness, Stop2 residency, retention, current,
oscillator accuracy or wake latency. All physical checks remain `not_executed`.
No automatic hardware execution or destructive action is provided.

Native 测试在公开 SDK 边界验证应用策略；定向 ARM 构建检查本独立 C11 image 与
自有内存预算。两者都不证明真实复制正确性、Stop2 驻留、保留、电流、晶振精度
或唤醒延迟。全部物理检查保持 `not_executed`，不提供自动实物执行或破坏性动作。
