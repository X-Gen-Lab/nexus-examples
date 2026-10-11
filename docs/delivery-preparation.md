# Delivery preparation and retained failures

This preparation leaves `external/nexus` and `dependencies/nexus.lock.json`
unchanged. Their current commit is
`6fc1ed75ba4e13d53c5b93c15ebaa73baaae9a21`, with tree
`069604a85d8c4dfa075fabe2daae133a37e690fd`. The source/dependency checker verifies
the clean SDK, its Gitlink and actual lock. The examples working tree still
contains development changes and is not a sealed candidate.

The new product fault policy depends on public `nexus/core/fault.h` and
`nx_fault_observe`, which that pin does not provide. A real full `native-debug`
build rejects the missing header. No replacement declaration, copied Core
implementation or filtered complete-run claim is supplied. The applicable
current-pin execution rebuilt four unaffected finite applications and the full
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

The seven authored presets select Native or the three maintained F407/F470
reference Boards with baremetal/FreeRTOS. The MCU examples select the reviewed
LED and console routes. Native sensor, journal and protocol examples do not
create sensor wiring, a MCU Flash region or RS485 direction timing on those
Boards. New Core support remains subject to the selected SDK's support and
qualification documents; a common interface alone does not expose hardware.

Upcoming Core integration includes the selected Linux/three-board M4 ThreadX
backend, opt-in shallow timebase sleep, USB PIO, raw Ethernet, timer mechanisms
and a narrow GPIO broker. These mechanisms do not add ThreadX/USB/Ethernet
application presets here. G474's HSE24 software reference is a separate
GPIO/LPUART vertical, not a fourth physically qualified example Board. A GPIO
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

The coordinated final step updates the Gitlink and source lock to the same
reviewed clean Core commit. It then verifies all seven authored presets:

1. Complete unfiltered Native catalogue: five finite apps and both Google
   targets, with fresh case identities, raw exits and no skipped cases.
2. Six board/backend builds, each producing the two real ARM ELF targets,
   with independent memory budgets, vectors, selected configuration and source
   identities; twelve images in total.
3. Exact SDK dependencies, source closure, format/comment/text checks and
   normal commit hooks. New clean execution cannot inherit an older run.

The preparation manifest records its partial execution and blocked full build.
It is explicitly not final SDK qualification, GitHub CI completion or physical
HIL. Final release evidence must bind the new clean examples commit and SDK pin.
