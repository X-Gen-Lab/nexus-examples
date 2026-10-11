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

At startup, any failed admission/task creation retains global storage. A real
product owns startup rollback or fail-stop policy before proceeding. Once a task
is created, do not destroy its notification or context until its proper join.

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
The current fixture also compiles the production backend policy for all three
backends and executes eleven GoogleTest cases.

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
