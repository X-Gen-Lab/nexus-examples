# Nexus 0.1.0 source preview integration

These examples consume the first Nexus 0.1.0 Source SDK developer preview,
dated 2026-10-15 (Asia/Shanghai). The formal Gitlink and
`dependencies/nexus.lock.json` select the same final Nexus main source:

| Identity | Exact value |
| --- | --- |
| Nexus commit | `58297516d707fdee117bd27b5df6af09d72edd2e` |
| Nexus tree | `06feab8ef3e89df60b4cdc5671c66ab0e3c08c21` |
| SDK semantic version | `0.1.0`, authored only in its compatibility JSON |
| SDK consumption | Source `add_subdirectory`, rebuilt with the application |
| Source interface epoch / authored TOML schema | `1` / `2` |
| Independent examples project version | `1.0.0` |
| Physical MCU HIL | `not_executed` |

The examples version does not select an SDK ABI or replace the full commit/tree
pin. The SDK's installed CMake package uses exact version matching; these examples
consume the selected source checkout directly. Precompiled objects from other
configuration, CPU/FPU, enum or float-ABI choices cannot be mixed because version
labels happen to agree.

## Maintained application selections

The existing eleven presets are unchanged: one Native build and five exact
software Board references with baremetal and FreeRTOS backends. Qiming F407ZG,
Sky F407VE and Liangshan GD32F470ZG retain their authored LED/console selections.
H743 selects HSI64, Cache off and IRQ UART; U575 selects MSIS4, TZEN=0 and IRQ
UART. Neither modern example selects DMA, enabled Cache or paired TrustZone
images. G474, ThreadX, USB, Ethernet and other SDK mechanisms do not acquire an
application preset through this pin update.

`platform_reference` and `uart_owner_runtime` each produce an ARM image for every
Board/backend selection. Native additionally executes the finite SPI sensor,
file-journal and authorized Modbus examples and the three complete GoogleTest
programs. Applications continue to own tasks, static storage, progress, admission,
shutdown, drain, recovery and any product policy.

## Tools and fresh software evidence

Prepare the recursive dependencies and tools using the repository's normal
[setup and build commands](../README.md#开发与构建). Linux x86_64 is the executed
integration host. The execution receipts record the actual Python, CMake, Ninja,
Native compiler, formatter, pre-commit and checksum-locked GNU Arm tools. A tool
lock or minimum version declaration alone does not prove their execution.

The current pin requires the complete existing Python suite, full Native CTest
and discovered Google cases, all ten ARM Board/backend builds, both images'
resource checks and the normal pre-commit/commit-msg hooks. The formal source
checker requires a clean SDK, the matching staged Gitlink and lock/tree, all
maintained recursive dependencies and the reviewed GD32 import. No local source
path override bypasses these identities.

Use the actual integration pull request and its
[workflow execution](../.github/workflows/ci.yml) to locate source-specific
commands, nonzero or failed attempts, logs, fresh reports and retained artifacts.
The build helper records commands and exits in each preset's `build.log`.
Native retains fresh `ctest-results.xml` and Google discovery/results; each ARM
selection retains ELF/BIN/map, resolved configuration and resource reports for
both applications. A successful upload does not replace a passed check, and
temporary CI retention is not a long-term storage guarantee.

Historical [SoC qualification](soc-refinement-results.json) remains bound to
examples `3de85238` and Nexus `98270512`. Its counts and the older failure records
remain unchanged. They do not qualify the new SDK pin or supply this integration's
execution results. Read the actual new reports rather than reusing those counts.

## Physical and release boundaries

All HIL work plans remain unbound software preparation. There is no connected
Board/probe/serial station execution in this integration. Models, linked ARM
images and SDK consumption do not prove electrical behavior, clock accuracy,
IRQ/DMA timing, Cache coherence, TrustZone isolation, power-cut recovery or safe
outputs. No flashing or equipment action occurs automatically.

The developer preview supplies software integration evidence. Product safety,
certification, recovery policy, signed product acceptance and LTS commitments
remain external responsibilities. See [delivery preparation](delivery-preparation.md)
for the retained ownership, fault and station boundaries.
