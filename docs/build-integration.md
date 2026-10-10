# Source SDK integration

Examples consume the pinned SDK with `add_subdirectory(NEXUS_SOURCE nexus)`.
The SDK exports `Nexus::Core`, `Nexus::IO`, `Nexus::Arch`, `Nexus::Platform`,
OS backend and optional component targets. Its `nexus_add_firmware()` helper
adds MCU startup, selected providers, linker configuration and real ELF budget
checks. Application sources remain in this repository.

`assemblies/*.toml` selects one Board package, clock profile, OS backend,
controllers, endpoints, IRQ policy and memory budget. Unsupported routes,
conflicting resources, contradictory configuration and malformed inputs fail
configuration. The emitted `nexus/generated/resolved.json` is the only effective
configuration. `bindings.c` exports static readonly faces and pure typed factory
getters. Every selected instance has its own mutable context and shares its
provider's readonly operation table.

An application includes generated `nexus_bindings.h`, `nexus_factory.h` and
`nexus_config.h`. `nx_factory_uart(NX_UART_ID_UART0)` returns one statically
selected UART identity. SPI/I2C controller getters return the controller;
`nx_factory_spi_device` and `nx_factory_i2c_device` return independent endpoint
identities. Invalid IDs return NULL. Lookup is safe before startup and performs
no allocation, initialization, name search or hardware access.
Calling `nx_platform_start()` reports primary failure, cleanup failure and any
remaining effects. Only after success may the application start its static
FreeRTOS tasks or enter its bare-metal loop. It owns all startup/error policy.

Native uses the same public contracts with explicit host models. Generated
instances use `nx_native_uart_model_receive(port, ...)` and the corresponding
instance-specific fixture helpers. Legacy default-fixture helpers do not target
an application's generated instances. MCU providers
remain selected at link time. ABI flags are propagated through public Core;
consumer and provider must agree on enum size, CPU/FPU and float ABI. This is a
source SDK rebuilt for each configuration, not a binary ABI compatibility claim.

Use `scripts/build.py --preset <name>`. The script only forwards CMake/CTest,
records commands and nonzero exits, removes stale JUnit and rejects empty or
all-skipped execution. Tool versions come from SDK dependency locks, including
CMake 3.31.6, Ninja 1.13.2, clang-format 14.0.6 and pre-commit 4.3.0.

`--nexus-source` supports another complete checkout. `test_repository.py` still
verifies the formal commit/tree, example Gitlink, clean dependencies and GD32
import. Explicit candidate validation requires a full SHA and matching origin;
it does not rewrite or promote the formal lock. Dirty joint-development builds
require temporary external Board paths and cannot serve as release evidence.
