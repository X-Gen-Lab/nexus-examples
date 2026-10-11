# Explicit multi-producer UART owner

`apps/uart_owner_runtime` composes the public typed factory, `UARTOwner`,
`BusOwner`, a short Arch guard and caller-owned OS objects. Nexus supplies the
mechanisms; this repository supplies producer count, payloads, execution tasks,
priorities, stack budgets and stop policy. No provider, SDK implementation or
PCB route is copied into the application.

Two producers each retain their own request, operation descriptor, payload and
completion notification. They submit through the same bounded two-slot owner.
Only the owner calls UART start/service/cancel. Producers never service the UART
inside a waiting predicate. All deadline values use the selected monotonic
provider domain; notifications remain hints and request `SETTLED` is authoritative.

## Execution

| Backend | Application-owned execution |
|---|---|
| Native | Two real producer pthreads, one real owner pthread, main coordinator |
| FreeRTOS | Two producer tasks, one owner task, one stop coordinator task |
| Baremetal | Two cooperative producers, one explicitly pumped owner loop |

Native disables automatic model TX completion, observes an active request and
then closes admission. This exercises active cancellation and provider drain,
not a simulated physical IRQ or wire timing measurement. The actual native UART
provider and actual pthread notification/task adapters execute in the example.

The owner pumps periodically even if a hint is coalesced or publishing fails.
The example's 1 ms wake interval is an explicit product choice, not a platform
latency guarantee. IRQ wakes use the actual generated board console priority and
selected kernel ceiling. Only Native model injections are synthetic; the six ARM
builds retain the existing reviewed board console route.

FreeRTOS may observe a genuine successful completion before stop; that first
terminal result remains valid. The example accepts SUCCESS or CANCELLED and
never rewrites a completion into cancellation after settlement.

## Stop and storage reclamation

The coordinator invokes a product-owned monotonic lifecycle:

1. Close owner admission and request cancellation of accepted operations.
2. Keep the owner running while producers observe settlement and join.
3. Request owner stop after producers have joined, then join the owner.
4. Verify idle includes the absence of in-flight completion publication.
5. Detach UART IRQ wake and stop RX/hardware publishers.
6. Destroy caller-owned completion, coordinator and owner notifications.
7. Native/Baremetal may stop the platform. A running FreeRTOS scheduler
   retains platform clocks/Tick; the coordinator parks with status available.
   Only an explicit product reset or complete scheduler-stop port permits
   removing those infrastructure clocks.

Each milestone advances only after success. A timeout or failed quiescence retains
all storage and stops reclamation. Callback implementations track their own
partial progress so already destroyed objects are not retried. QUARANTINED remains
a retained borrow and requires product recovery; the example supplies no automatic
reset, destructive Flash operation or forced thread cancellation.

`quarantine_policy.c` is an explicit application policy used at every actual
owner-progress site in `main.c`. BUSY remains normal waiting. A real service
error closes admission and publishes `FAULT_OBSERVED`; a reversed observation
clock, arithmetic overflow or the authored cleanup budget publishes
`ESCALATED_RETAINED`. The reference budget is 100 service steps and 100 ms after
the first fault. These limits govern escalation, not permission to reclaim.

Every later progress call still executes the real owner service step. It never
kills the owner, resets hardware, replays the operation or frees quarantined
buffers. Actual owner idle publishes `LOAN_DRAINED`, after which the ordinary
join, IRQ-detach, hardware-stop and notification-destruction sequence still
applies. The bounded fault sink records stage/status and wakes the coordinator;
product-specific safe outputs and recovery decisions remain external.

`tests/quarantine_policy_test.cpp` exercises the production policy and actual
BusOwner with GoogleMock external hardware/fault ports. Six new cases had a true
stub RED before implementation and cover BUSY, retained step/deadline exhaustion,
clock reversal, deadline-add overflow and later successful drain. The shared
lifecycle Google binary now runs 28 cases. This development iteration preserves
the existing clean SDK pin; it does not claim physical timing or a new release
qualification until the complete clean-source integration runs.

`bootstrap.c` implements a separate partial-startup protocol. The application
records only successful notification, owner and worker creations, along with
retained platform and publisher effects. A notification failure destroys only
the preceding creations, in reverse order. Partial platform startup preserves
the platform's own remaining-effects result and retries its explicit stop.

Native task creation starts a real thread immediately. Failure creating a later
worker first closes admission, joins the created producers while the owner still
drains their accepted requests, then joins the owner. A producer rejected after
admission closes has no loan; its operation result does not prohibit joining.
UART publishers are quiesced before notifications and platform storage are
reclaimed.

FreeRTOS creation before `vTaskStartScheduler()` has a different meaning: none
of those static tasks has executed. Rollback uses the public
`nx_freertos_task_discard_before_scheduler()` in reverse creation order. It does
not wait for entry completion or call join with a stopped scheduler. The
`scheduler_started` guard is set before attempting scheduler startup; it does
not infer whether the kernel is currently running. Once attempted, prestart
rollback rejects the call. Ordinary cooperative shutdown under a running
scheduler retains kernel clocks/Tick.

An unexpected return from `vTaskStartScheduler()` enters the explicit terminal
policy in `scheduler_return.c`. It publishes startup stage 8 and `NX_ERROR_IO`
before calling the product fail-stop sink. It preserves all application task,
TCB/stack, notification, accepted-buffer, IRQ and platform storage. Kernel
startup may already have created its Idle task, masked interrupts, marked itself
running, installed priorities and configured Tick/FP state. A current
`NOT_STARTED` observation would not prove that tasks never executed or that
these effects were undone. The example therefore does not call prestart
discard, join, platform stop, retry creation or return to the C runtime.

The default terminal sink masks configurable interrupts and invokes the public
weak FreeRTOS fail-stop hook. It retains clocks and static storage through a
non-returning product diagnostic/reset action. If an override unexpectedly
returns, the example remains parked. Safe external outputs and a complete reset
or scheduler-stop port still require explicit product implementation; this
policy does not establish hardware safety or reclaim kernel-owned effects.

Successful rollback milestones are not repeated. A failed join, drain, IRQ
detachment, notification destruction or platform stop retains its effects and
static storage. The example records the error and parks for an explicit product
recovery decision; it does not free borrowed buffers or silently reset equipment.

## Budgets and validation

Native uses three explicitly aligned 64 KiB host stack buffers. Extra host
instrumentation may require a larger authored stack budget; it never inherits
the MCU budget. Its authored RAM
budget is therefore separate from the MCU reference budget. Native pthread library
bookkeeping is not zero-heap MCU evidence. MCU task arrays declare 8-byte alignment and each receive 256
`StackType_t` words; that reference budget must still be checked for workload and
exception nesting on equipment.

`tests/owner_lifecycle_test.cpp` uses the SDK's locked offline GoogleTest/GoogleMock
archive. It tests the actual BusOwner and application lifecycle source, mocking
only external join and hardware-quiescence actions. It covers admission closure
before join, deadline failure retention, owner join failure, premature publication
reclamation, resumable quiescence and invalid inputs. The initial six cases had a real
RED execution before implementation. Additional teardown regressions had a
separate RED: a live kernel must retain clocks/Tick, while a stopped kernel may
stop only after reclaim, failures retain the phase and repeat stop is suppressed.
The fixture also compiles the production backend policy for all three backends.
`tests/owner_bootstrap_test.cpp` exercises partial creations, unstarted/running
task distinctions, reverse rollback, retry and retained loans. Eight initial
startup contracts had an executed RED before implementation. A further fixture
creates actual Native owner/producer threads, injects a failed next creation,
and verifies queued settlement, joins and notification destruction. Its 256 KiB
host stacks serve instrumented host execution, not MCU stack qualification.
Fresh discovery establishes the current case count.
`tests/scheduler_return_test.cpp` had two executed RED cases before the terminal
policy was implemented. The actual C11 policy publishes diagnostic state and
retains the irreversible guard before a mocked external terminal sink. The
test sink throws a host-only marker so tests can inspect retained storage; it
does not execute or simulate the MCU scheduler-start hardware path.

Native CTest executes this fixture and the actual threaded application. The
GoogleTest runner discovers all cases, executes them without user filters and
compares fresh XML case identities with that discovery; zero/skipped/stale or
changed-case reports fail. Firmware
is C11 and does not link GoogleTest, GoogleMock or a C++ runtime. ARM ELF resource
checks establish linking and budget; physical timing, register context, stack peak,
low-power restoration and safe-output acceptance remain unexecuted until actual
boards are attached.

The staged TDD hook verifies the formal clean SDK before and after execution,
runs every maintained Native CTest and tooling test, and compares Google case
identities with fresh unfiltered discovery. Unstaged behavioral changes, changed
index content, candidate SDK overrides and filtered or stale evidence reject.
The hook never stages files or claims historical RED order or hardware results.

```sh
python scripts/build.py --preset native-debug --test
python scripts/build.py --preset stm32-qiming-freertos
```

Use the same six board/backend presets for `uart_owner_runtime`. Final integration
qualification requires the examples Gitlink and `nexus.lock.json` to name the same
clean, verified core commit. A development build against uncommitted core source
is explicitly nonpublishable and cannot replace that final source identity.
