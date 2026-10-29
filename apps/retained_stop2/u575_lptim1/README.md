# U575 one LPTIM1 tuple across Stop2 / 单组 LPTIM1 元组跨 Stop2

This standalone C11 application selects exact STM32U575ZIT6Q TZEN0,
baremetal and MSIS4. It explicitly links `Nexus::U575Stop2Lptim1` and submits
one immutable SRAM4 tuple `{CCR1=37, RCR=0, ARR=2047}` through LPDMA1 channel0
to LPTIM1. It uses the same concrete Stop2 owner and its RUN ClockDomain,
ordinary retained RTC time, and the shared application lifecycle helper.
The WUT-only and SRAM4-copy Stop2 targets are mutually exclusive alternatives.

本独立 C11 应用选择精确 STM32U575ZIT6Q TZEN0、裸机及 MSIS4，显式链接
`Nexus::U575Stop2Lptim1`，通过 LPDMA1 channel0 向 LPTIM1 提交一组不可变
SRAM4 元组 `{CCR1=37, RCR=0, ARR=2047}`。应用使用同一个具体 Stop2 owner
及其 RUN ClockDomain、普通保留 RTC 时间与共享应用生命周期 helper。
WUT-only、SRAM4-copy 与本 Stop2 模式互斥。

This is the public SDK's finite single-tuple LPBAM profile. It establishes no
general LPBAM engine, repeated waveform, GPIO route or physical PWM playback.
The selected board fixture provides no known output-pin route. Successful
compilation and `wait_executed` do not prove Stop2 residency, retention, timing,
current or power savings; all physical HIL remains `not_executed`.

此应用使用公共 SDK 的有限单元组 LPBAM profile，不提供通用 LPBAM 引擎、
重复波形、GPIO 路由或实际 PWM 播放。所选 board fixture 没有已知输出引脚路由。
编译成功和 `wait_executed` 不证明 Stop2 驻留、保留、时序、电流或节能；全部实物
HIL 仍为 `not_executed`。

## Build and memory / 构建与内存

Use the exact clean SDK selected by this private repository's Gitlink and
source lock. From the repository root, set `NEXUS_SOURCE` to that SDK, make the
pinned ARM compiler and CMake/Ninja available, then build only this target:

使用本私有仓库 Gitlink 和源码锁选定的精确干净 SDK。在仓库根目录把
`NEXUS_SOURCE` 设为该 SDK，使固定 ARM 编译器及 CMake/Ninja 可用，然后只构建
本目标：

```sh
cmake -S apps/retained_stop2/u575_lptim1 -B build/retained-stop2-lptim1 -G Ninja \
  -DNEXUS_SOURCE="$NEXUS_SOURCE" \
  -DCMAKE_TOOLCHAIN_FILE="$NEXUS_SOURCE/cmake/toolchains/arm-gcc.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/retained-stop2-lptim1 \
  --target retained_stop2_lptim1 --parallel 1
```

`assembly.toml` owns a 4096 B main stack, zero libc heap, a 16384 B ordinary
static-RAM limit, a 65536 B Flash-load limit and a separate 512 B SRAM4 limit.
The generated ELF resource check enforces these budgets; the limits are not
measured usage. No vendor include, private provider include or copied driver is
used. The recipe adds no worker, queue, allocation or runtime operations table;
`EXAMPLE_STOP2_COMPOUND=1` selects two direct C application hooks at build time.

`assembly.toml` 定义 4096 B 主栈、零 libc heap、16384 B 普通静态 RAM 上限、
65536 B Flash-load 上限及独立 512 B SRAM4 上限。生成 ELF 时的资源检查强制执行
这些预算；预算不是实测占用。应用不包含厂商或 provider 私有头文件，不复制
driver，不增加 worker、queue、分配或运行时操作表。
`EXAMPLE_STOP2_COMPOUND=1` 在构建时选择两个直接 C 应用 hook。

| Storage / 存储 | Placement and lifetime / 位置与生命周期 |
| --- | --- |
| Tuple / 元组 | 12 B, four-byte aligned SRAM4 DATA; copied from Flash at reset, unchanged from prepare through settlement / 四字节对齐 SRAM4 DATA，复位时从 Flash 复制，prepare 至 settlement 保持不变 |
| Generated workspace / 生成 workspace | Separate 48 B, four-byte aligned SRAM4 NOINIT; exactly two owner-generated LLIs, never read before owner initialization / 独立 48 B 四字节对齐 SRAM4 NOINIT，仅两条 owner 生成的 LLI，owner 初始化前不读取 |
| Receiver owner / receiver owner | 104 B on ARM32, ordinary SRAM1, alive through public stop and all joins / ARM32 为 104 B，普通 SRAM1，保留至公共 stop 及全部 join |
| Request, faces, clock, snapshots, helper and stack / 请求、face、时钟、快照、helper 与栈 | Outside tuple/workspace extents; retained control storage through final access and observer joins / 与元组及 workspace 不重叠，控制存储保留至最终访问及 observer join |

SRAM4 is the exact nonsecure `0x28000000..0x28003fff` 16 KiB window. DATA and
NOINIT placement use public `NX_MEMORY_SRAM4_*` attributes. Selected reset
startup prepares the SRAM4/LPDMA run profile even when the main authorization
remains zero. Retention and bus-master access still require the external boot
and public component contracts. Runtime recovery must never try to restore lost
SRAM4 by changing power-down fields.

SRAM4 是精确非安全 `0x28000000..0x28003fff` 的 16 KiB 区域。DATA 与 NOINIT
使用公共 `NX_MEMORY_SRAM4_*` 属性。即使 main 的授权变量保持零，所选复位启动
仍准备 SRAM4/LPDMA 的 RUN profile。保留和 bus-master 访问仍需外部 boot 及公共
组件合同成立；运行时 recovery 绝不能通过更改掉电字段来“恢复”丢失的 SRAM4。

## Prepared boot and opt-in / Boot 准备与显式启用

At `main`, the zero-initialized `example_stop2_lptim1_authorization` makes the
application return before constructing owners or starting Platform. Only after
reviewing the public [LPTIM1 LPBAM contract](../../../external/nexus/docs/design/u575-lptim1-lpbam.md)
and [retained-time/Stop2 contract](../../../external/nexus/docs/design/u575-retained-time.md)
may a debugger halt at `main` and set it to `0x4e584c31`. This switch is an
example opt-in, not security authorization or boot preparation. Normal entry
requires the debugger to be detached or otherwise externally excluded.

进入 `main` 时，零初始化的 `example_stop2_lptim1_authorization` 使应用在构造
owner 或启动 Platform 前返回。完整审查公共 [LPTIM1 LPBAM 合同](../../../external/nexus/docs/design/u575-lptim1-lpbam.md)
及 [保留时间／Stop2 合同](../../../external/nexus/docs/design/u575-retained-time.md)
后，才可在 `main` 断点设置为 `0x4e584c31`。该开关只是示例显式启用，不是安全
授权，也不准备 boot。实际入口仍要求调试器断开或由外部排除其影响。

External boot supplies exclusive valid BCD24h LSE32768 crystal calendar,
PRER127/255, BYPSHAD, conservative drive, RTCEN/RTCAPBEN, PWR access and DBP.
It preserves the RTC/LSE provenance and excludes all other calendar writers,
alarms, RTC IRQ users and PC13 transitions. Review the exact silicon revision
and errata. No factory RTC/AlarmA owner or AlarmA-copy initializer is selected.

外部 boot 提供独占且有效的 BCD24h LSE32768 晶体日历、PRER127/255、BYPSHAD、
保守 drive、RTCEN/RTCAPBEN、PWR 访问及 DBP，维持 RTC/LSE provenance，并排除
其他日历 writer、alarm、RTC IRQ 用户与 PC13 跳变。必须审查精确硅片版本及勘误。
应用不选择 factory RTC/AlarmA owner，也不使用 AlarmA-copy 初始化器。

Require exact TZEN0, unswapped Flash, effective MSIS4/MSIK4, equal buses, true
Range4, caches off and the selected reset-only SRAM4/LPDMA startup. Keep SRAM4
powered/retained and SRAM4EN/LPDMA1EN live through all joins; retain the ordinary
control SRAM and MSP as required by Stop2. All DMA channels/vectors start quiet,
no other LPDMA channel runs, and LPDMA1 channel0 is exclusive. The entire LPTIM1
controller is exclusive, cold/reset, initially unclocked with its reset kernel
mux. Exclude external timer writers, IRQ users and resets. RUN LPTIM2 must be
disabled and joined before entry.

入口要求精确 TZEN0、未交换 Flash、有效 MSIS4/MSIK4、相同总线分频、真实 Range4、
cache 关闭及所选仅复位 SRAM4/LPDMA 启动。保持 SRAM4 供电及保留，SRAM4EN 与
LPDMA1EN 在全部 join 前持续有效；普通控制 SRAM 与 MSP 按 Stop2 合同保留。
全部 DMA channel/vector 初始静止，没有其他 LPDMA channel 运行，LPDMA1 channel0
独占。整个 LPTIM1 controller 独占、cold/reset，初始访问时钟关闭且 kernel mux
处于 reset 值。排除外部 timer writer、IRQ 用户及 reset。RUN LPTIM2 必须在入口
前禁用并 join。

One privileged Thread serializes every application/component call. No scheduler
may ever have started. Entry uses MSP, CONTROL0, PRIMASK1, BASEPRI0 and
FAULTMASK0 after all enrolled RUN owners drain. Exclude other peripherals,
IRQs/EXTI/SysTick, watchdogs, other masters, NMI/HardFault, debugger and
asynchronous clock/power writers externally. Quiet registers and linkage cannot
prove electrical silence or those ownership facts.

单一特权 Thread 串行调用所有应用／组件接口，调度器从未启动。全部注册的 RUN
owner 排空后，入口使用 MSP、CONTROL0、PRIMASK1、BASEPRI0 与 FAULTMASK0。
由外部排除其他外设、IRQ/EXTI/SysTick、watchdog、其他 master、NMI/HardFault、
调试器与异步时钟／电源 writer。静止寄存器或链接不能证明电气静默和上述所有权。

## Ownership and one cycle / 所有权与一次闭环

1. Construct ordinary retained time with the concrete Stop2 identity, initialize
   Stop2 and the pure receiver, then explicitly call receiver `start` in
   unmasked privileged Thread. Start owns cold LPTIM1 access gates, LSE kernel
   selection and only needed LSESYSEN; it does not start the timer or write timer
   registers. Any partial start still requires joined public `stop`.
2. Initialize the request, acquire a successful retained snapshot and prepare
   the tuple/workspace metadata with `NX_DEADLINE_NEVER`. Prepare creates no
   borrow and writes no tuple/workspace. Finite transfer length is exactly 12 B;
   NEVER does not make the DMA transfer repeat or bound physical sleep.
3. Acquire the exact Platform RUN lease and call Platform start. The shared
   helper closes admission, actually stops Platform, releases that exact lease,
   proves the closed barrier quiescent and saves/disables the incoming mask.
4. The direct enter hook calls `nx_stm32u575_stop2_lptim1_enter` once with 4096
   RTC/16 escape ticks and `poll_limit=10000`. The concrete owner enables the
   counter after its retained-time delay and fresh initial write-OK proof,
   admits one request13/BREQ1 CCR1 write, and generates two finite LLIs for RCR
   then ARR. RCR is zero; no repeated update is admitted.
5. Preserve the same mask, barrier and static storage through standard concrete
   `recover` calls until `remaining_effects==0`. Recovery executes no further
   WFI. Only accepted requests require acquire-observed `nx_request_result`;
   successful operation also requires exactly `sizeof(s_tuple)` transferred.
   Before permitting RUN resume, JOIN checks the original DMA wake and WFI,
   absence of WUT escape, nonnull unchanged retained identity, matching nonzero
   generation and monotonic retained samples.
6. Explicitly join receiver `stop` before restoring the mask or reopening RUN.
   The concrete owner handles UEDE/DIEROK withdrawal, DMA final-access proof and
   owned RCC reset shutdown of the counter; applications never clear ENABLE.
   Public stop separately restores the start-owned gates/mux/source effects.
7. Only complete successful drain and joins permit the helper to restore the
   incoming mask, reopen RUN, acquire a fresh lease, start a fresh Platform/Core
   epoch, then actually stop/release it. Finally stop the separate retained clock.

1. 使用具体 Stop2 identity 构造普通 retained time，初始化 Stop2 及纯 receiver，
   在未屏蔽的特权 Thread 显式调用 receiver `start`。start 拥有 cold LPTIM1 访问门、
   LSE kernel 选择与仅按需添加的 LSESYSEN；它不启动 counter，不写 timer 寄存器。
   部分 start 失败也必须通过公共 `stop` join。
2. 初始化请求，取得成功保留快照，使用 `NX_DEADLINE_NEVER` 准备元组／workspace
   元数据。prepare 不建立借用，也不写元组／workspace。有限传输恰好 12 B；NEVER
   不会令 DMA 重复，也不为物理睡眠提供时限。
3. 获取精确 Platform RUN lease 并调用 Platform start。共享 helper 关闭 admission，
   真正 stop Platform，释放同一 lease，证明关闭的 barrier quiescent，再保存并屏蔽
   原始中断 mask。
4. 直接 enter hook 仅调用一次 `nx_stm32u575_stop2_lptim1_enter`，使用 4096 个
   RTC/16 escape tick 与 `poll_limit=10000`。具体 owner 在保留时间延迟和全新初始
   write-OK 证明后启用 counter，接纳一次 request13/BREQ1 CCR1 写入，并生成
   RCR、ARR 顺序的两条有限 LLI。RCR 为零，不接纳重复更新。
5. 保持相同 mask、barrier 及静态存储，调用标准具体 `recover` 直到
   `remaining_effects==0`。recovery 不再次 WFI。只有 accepted 请求才要求通过
   `nx_request_result` acquire 观察 settlement；成功还要求 transferred 恰好等于
   `sizeof(s_tuple)`。允许恢复 RUN 前，JOIN 检查原始 DMA wake 与 WFI、无 WUT
   escape、非空且不变的 retained identity、相同且非零 generation 与单调保留快照。
6. 恢复 mask 或重开 RUN 前，显式 join receiver `stop`。具体 owner 负责
   UEDE/DIEROK 撤回、DMA 最终访问证明及自有 RCC reset 的 counter 关闭；应用不
   清除 ENABLE。公共 stop 单独恢复 start 拥有的访问门／mux／source effects。
7. 只有完整成功排空及 join 后，helper 才恢复原始 mask、重开 RUN、获取新 lease，
   启动新的 Platform/Core epoch，随后真正 stop/release。最后停止独立保留时钟。

The 4096 ticks request roughly two seconds of WUT escape. WUT/deadline/cancel
never revoke a borrow. ACCEPTED retains request, tuple and full workspace
through acquire-observed SETTLED and external observer joins, including setup
failure. REJECTED retains no new reference. A quarantined loan remains borrowed
until actual external system reset; it is never reinitialized or overwritten.
Success proves finite DMA completion and fresh synchronized CCR1/RCR/ARR
write acknowledgements captured before DIER withdrawal, not a full PWM period.

4096 tick 请求约两秒 WUT escape。WUT、deadline 或 cancel 都不会撤销借用。
ACCEPTED 即使随后 setup 失败，也保留 request、tuple 及整个 workspace，直到
acquire 观察 SETTLED 且外部 observer join。REJECTED 不保留新引用。quarantined
loan 在真实外部系统 reset 前仍被借用，绝不重初始化或覆盖。成功证明有限 DMA
完成，且 DIER 撤回前捕获到全新同步 CCR1/RCR/ARR 写应答，不证明完整 PWM 周期。

## Failure, resume and observations / 失败、恢复与观察

The helper preserves first failure. Failed Platform stop/release retains its
exact RUN lease; failed concrete recovery retains mask and barrier; failed
request/public-stop joins retain the same static objects and do not reopen
RUN. A fully drained failure restores the incoming mask and finishes with RUN
admission closed, without restarting Platform. Initial partial Platform start
still passes through actual stop/release and the receiver join. Failures before
RUN acquisition join even partial receiver start, then stop the retained clock.

helper 保留首次失败。Platform stop/release 失败保留精确 RUN lease；具体
recovery 失败保留 mask 和 barrier；request／公共 stop join 失败保留相同静态
对象且不重开 RUN。完全排空后的失败恢复原始 mask，以 RUN admission 关闭状态
结束，不重启 Platform。初次 Platform 部分启动仍必须真正 stop/release，并 join
receiver。获取 RUN 前的失败也 join 部分 receiver start，然后停止保留时钟。

Main allows at most 64 progress attempts, and pre-admission receiver cleanup
allows at most 64 stop attempts. Unfinished work parks with storage and retained
ownership; these counts never authorize reclamation. One WFI can wait forever
if physical wake fails. Poll limits bound observations per phase, not elapsed
time. The declared 60-second retained observation gap must externally cover the
whole entry, physical wake latency and recovery interval. Missed gaps can taint
the clock; no stale timestamp or previous Core TIM2 deadline is reused.

main 最多推进 64 次；接纳前 receiver 清理最多尝试 stop 64 次。未完成即原地
保留存储与所有权；次数上限绝不授权回收。物理唤醒失败时，一次 WFI 可永久等待。
poll limit 限制每阶段观察次数，不是耗时上限。所声明的 60 秒保留观察间隔必须
由外部保证覆盖整个入口、物理唤醒延迟及 recovery。错过间隔可使时钟 tainted；
不重用旧时间戳或上一 Core TIM2 epoch 的 deadline。

An unknown clock/access profile permits no RTC/LPTIM/DMA MMIO and retains
effects. Owned LPTIM1 reset failure remains retryable only after DMA final-access
proof. Observed external LPDMA or LPTIM reset during a loan permanently
quarantines it until actual external system reset; deassertion and quiet reset
defaults do not release storage. The application parks instead of inventing
private register recovery.

未知时钟／访问 profile 禁止 RTC/LPTIM/DMA MMIO，并保留 effects。只有 DMA 最终
访问证明完成后，自有 LPTIM1 reset 失败才可重试。借用期间观察到外部 LPDMA 或
LPTIM reset，会永久 quarantine，直到真实外部系统 reset；reset 解除和静止默认
值都不能释放存储。应用原地保留，不自行编造私有寄存器恢复。

Observe `example_stop2_lptim1_result`, `_phase`, `_restarted`, `_accepted`,
`_dma_wake`, `_request_result`, `_transferred` and `_elapsed_us` using the common
`example_stop2_lptim1` prefix. Elapsed is published only after full successful
completion from the original same-generation retained snapshots, at
3906.25 microsecond resolution. DMA wake and WFI observations are software
evidence, not a physical residency measurement.

使用共同前缀 `example_stop2_lptim1` 观察 `_result`、`_phase`、`_restarted`、
`_accepted`、`_dma_wake`、`_request_result`、`_transferred` 及 `_elapsed_us`。
elapsed 仅在全部成功完成后，由原始同 generation 保留快照发布，分辨率为
3906.25 微秒。DMA wake 和 WFI 是软件观察，不是物理驻留测量。

Host tests cover the shared application policy at public operation boundaries;
the focused ARM target verifies this C11 composition and enforces its ELF
budgets. Neither substitutes for unexecuted physical RTC, wake, reset, waveform,
retention, timing, current, power-cut or Board electrical checks. Source and
artifacts remain in this private examples repository; no public SDK export is
part of this recipe.

host 测试在公共操作边界覆盖共享应用策略；聚焦 ARM 目标验证本 C11 组合并强制
执行 ELF 预算。两者都不能替代尚未执行的真实 RTC、唤醒、reset、波形、保留、
时序、电流、掉电及 Board 电气验收。源码与产物保留在本私有 examples 仓库，
本 recipe 不进行公共 SDK 导出。
