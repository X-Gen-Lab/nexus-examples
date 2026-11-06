# Delivery preparation and retained failures

The first part of this document retains the historical preparation against
`external/nexus` and `dependencies/nexus.lock.json` at commit
`6fc1ed75ba4e13d53c5b93c15ebaa73baaae9a21`, with tree
`069604a85d8c4dfa075fabe2daae133a37e690fd`. It describes that earlier development
snapshot, not the current formal SDK pin. Current identity comes from the actual
source lock and Gitlink; the source/dependency checker verifies both. Historical
raw evidence remains unchanged and cannot qualify the new software references.

That historical fault-policy development depended on public
`nexus/core/fault.h` and `nx_fault_observe`, which the recorded `6fc1ed75` pin
did not provide. Its real full `native-debug` build rejected the missing header.
No replacement declaration, copied Core implementation or filtered complete-run
claim was supplied. The applicable execution at that historical pin rebuilt
four unaffected finite applications and the full
lifecycle Google binary: five CTests passed, with all 28 discovered Google
cases executed under the selected ASan/UBSan build. This partial result does not
qualify the changed UART application or new fault-policy target.

The complete Python tooling suite ran 39 tests. A focused automation regression
also replayed the historical `pass_filenames: true` setting and failed before
restoring the exact current bytes and passing. The TDD hook processes the entire
index itself; pre-commit must not append changed paths to its argument list.
This explicit replay proves the regression boundary, not the historical order
of the original edit. It does not bypass source checks or stage files.

## Fault state and recovery responsibility

`fault_observer.c` implements the product's strong Core observer. It forwards
scalar data without dereferencing a failing filename. The caller-owned policy
claims and publishes the first diagnostic before invoking its explicit output
port. Nested or repeated faults cannot replay the output action or replace the
first record. A snapshot during the action reports an incomplete output result.

The example's port returns UNSUPPORTED and drives no pins. Absent or failed
actions remain UNPROVEN. Even a software SUCCESS would not prove the voltage,
timing or safe level of a physical output. The incoming task/ISR/privilege/mask
state is preserved; the fault path grants no new execution permissions and
cannot assume a functioning clock, kernel, transport or stack.

An accepted or quarantined request retains its descriptor, bytes and all
publisher references. A diagnostic, exceeded deadline, parked coordinator or
scheduler-start return is not settlement. The platform and OS controls remain
valid while their references remain live. A running FreeRTOS scheduler retains
clock/Tick infrastructure after normal I/O shutdown. A failed scheduler-start
attempt cannot be rolled back using the prestart discard API.

Products supply a separately reviewed recovery port. It must establish actual
controller/DMA inactivity and IRQ publication withdrawal, complete request
settlement and worker joins, and only then reclaim the applicable storage.
When those facts cannot be established, storage remains retained and the
product decides its external reset or maintenance action. The example neither
forces task cancellation nor converts a reset request into a fabricated
successful drain. Output-stage safety and post-reset recovery need their own
hardware and product acceptance.

The seven fault-policy Google cases and original application execution are
stored in [development evidence](../tests/evidence/fault-policy/README.md).
They used an explicitly selected development Core source, not the older formal
pin. Their original source snapshots, commands and reports stay immutable.

## Core capabilities and example selection

The seventeen authored presets select Native or eight exact software references
with baremetal/FreeRTOS: the three maintained F407/F470 Boards, H743/U575,
F103RB, GD32F303RC and G474RE. The MCU examples select only the declared
GPIO and console routes. GD32F303RC is a silicon software reference with no
asserted PCB or LED. Native sensor, journal and protocol examples do not
create sensor wiring, a MCU Flash region or RS485 direction timing on those
Boards. New Core support remains subject to the selected SDK's support and
qualification documents; a common interface alone does not expose hardware.

The SDK includes the selected Linux/three-board M4 ThreadX
backend, opt-in shallow timebase sleep, USB PIO, raw Ethernet, timer mechanisms
and a narrow GPIO broker. These mechanisms do not add ThreadX/USB/Ethernet
application presets here. G474's HSE24 software reference is a separate
GPIO/LPUART vertical and does not establish an additional qualified physical Board. A GPIO
broker is not a complete SVC/MPU/TrustZone user I/O transport. No unreviewed PCB
binding or physical support follows from these software mechanisms.

## HIL readiness

There is no connected probe, serial device, observed station or acquisition in
this delivery. A generated HIL/measurement/OS plan remains `prepared_unbound`
and every physical case remains `not_executed`. After the final SDK pin, use
its maintained `tools/hil` preparation and admission tools and their exact
schemas rather than inventing a second collector or editing a passed record.

The station owner must observe PCB/part/UID, probe serial, stable serial path,
wiring, supply, tool/configuration hashes and the exact ELF/resolved/resource
identities. It reviews numeric budgets and real workloads before explicitly
authorizing equipment acquisition. Measurements retain raw samples, instrument
configuration/calibration, clocks, load and IRQ priorities. Preparation or a
validated empty plan cannot stand in for waveforms, FPU context, clock/Tick
accuracy, stack peaks, watchdog reset, low-power restoration or safe outputs.
Destructive Flash/power-cut work requires a product-owned permitted region and
station procedure; it is never an automatic example startup action.

## Final software integration

Each coordinated SDK update changes the Gitlink and source lock together to
the same reviewed clean Core commit. Fresh integration must cover all seventeen
authored presets, including the eight exact MCU references under both MCU
backends:

1. Complete unfiltered Native catalogue: eight finite apps, all registered Google
   targets and two real FreeRTOS registrations, with fresh case identities, raw
   exits and no skipped cases.
2. Sixteen reference/backend builds, each producing the two real ARM ELF targets,
   with independent memory budgets, vectors, selected configuration and source
   identities; thirty-two images in total. The existing CI separately links the
   H743/U575 UART FIFO reference, without adding presets.
3. Exact SDK dependencies, source closure, format/comment/text checks and
   normal commit hooks. New clean execution cannot inherit an older run.

The historical preparation manifest records only its partial execution and
blocked full build. It is not qualification of a later SDK pin, GitHub CI
completion or physical HIL. New integration evidence must bind the actual clean
examples commit and matching SDK pin; historical passes and failures do not
qualify that tuple.


## Versioned developer recipes / 版本化开发入口

The manifest indexes Native/Qiming/Sky/Liangshan starters plus separate Qiming
core and V4.1 minimal projects with explicit hardware choices. Other recipes cover
two-endpoint SPI with real Native/FreeRTOS host tasks, and slow-consumer stream
recovery. Standalone MCU recipes deliberately distinguish an F407ZG silicon
fixture from a physical Board and select only the supported H743 continuous RX
mode. [The bilingual guide](developer-recipes.md) is the caller contract.
当前清单包含六个摘要绑定模板，其中启明核心板与 V4.1 独立配置全部 hardware
选项，保留 V3.1。另有两端点真实任务共享 SPI 和慢消费者流恢复；MCU 软件 fixture
与实板严格区分，仅选择实际支持的 H743 连续模式。新模板在 pin 更新及两条 SDK
消费路径验证前只属于开发准备，不增加已有构建或 HIL 资格。

Final identity ordering is platform commit P, examples lock/Gitlink update to P,
ordinary complete gates, then examples commit E. The delivery record binds P/E
and actual artifact identities. Development runs against an explicit uncommitted
SDK remain nonpublishable; this paragraph does not certify a pending pin or
reclassify `not_executed` physical work.
最终先完成平台 P，再同步 examples 锁与 Gitlink，普通完整 gate 通过后得到 E，
交付记录绑定两者和实际产物。开发源码运行不能作为正式资格，本段不证明待定 pin，
也不改变实板 `not_executed` 状态。

## 2026-10-24 source migration / 本轮源码迁移

This iteration is based on examples PR #10,
`fix/ci-host-deps-budget-20261022`, head
`7b7fea3e0bb0465a8a4e93ab8132a458c0373f31`. Its bounded host dependency
preparation (HTTP/HTTPS timeout, retries, apt update error mode and actual shell
failure regression) remains intact. The examples worktree was preserved while
fast-forwarding to that base; the new recipes do not discard the pending CI fix.
本轮以示例库 PR #10 的上述精确提交为基础，保留 host 依赖下载超时、重试、apt 索引
错误模式及实际 shell 失败回归；安全快进时保留了本轮全部应用和模板变更。

The dependency migrates from
`d0b8637453330070a052ededbd39bb1f597c76a9` to platform commit
`c8df1c49d4f771a55490b3a8be32a9d1f24faa1e`, tree
`d662c46e31e180a348d61ba1567a6487c28cc74d`. The lock and Gitlink must agree.
The platform completed its ordinary full commit gate before this pin update;
examples have their own ordinary commit gate and independently recorded results.
依赖从上述旧提交迁移到已完成平台普通完整提交门禁的 P；源码锁与 Gitlink 必须一致。
示例库仍独立执行自己的普通门禁并记录结果，不能借用平台测试数量作为应用验收。

The final delivery index records examples commit E and its own CI/artifact
identity after the ordinary commit. This document does not predeclare E or
turn development evidence into physical qualification. HIL: `not_executed`.
示例库普通提交完成后由交付索引关联 E 与实际 CI/产物，本页不预先编造 E，也不把
开发执行记录变成实板资格。HIL：`not_executed`。
