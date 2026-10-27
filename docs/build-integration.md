# Source SDK integration

Run the complete local software matrix through [local CI](local-ci.md). It reuses
this integration and the hosted workflow checkers; partial runs remain explicit.
完整本地软件门禁见[本地 CI](local-ci.md)，复用本节构建链路，部分运行明确标记。

Examples consume the pinned SDK with `add_subdirectory(NEXUS_SOURCE nexus)`.
The SDK exports `Nexus::Core`, `Nexus::IO`, `Nexus::Arch`, `Nexus::Platform`,
OS backend and optional component targets. Its `nexus_add_firmware()` helper
adds MCU startup, selected providers, linker configuration and real ELF budget
checks. Application sources remain in this repository.

The current integration consumes the Nexus 0.2 development iteration selected
by `dependencies/nexus.lock.json` and the matching `external/nexus` Gitlink.
The lock owns the exact commit and tree. The SDK includes the official STM32F4 LL
adaptation and optional caller-owned cold diagnostics. Its SDK metadata is
`0.2.0`; this does not move the published `v0.1.0` tag or qualify this changed
implementation with historical release evidence. This repository's independent
application project version remains `1.0.0`. Source interface epoch `1` and
authored TOML schema `2` remain separate contracts. See [release
integration](release-0.1.0.md) for the original published snapshot and its evidence
boundaries.

The selected platform uses typed configuration IR epoch `2` and resolved JSON
projection schema `1`. Its Python resolver returns immutable typed objects;
these examples consume the TOML CLI and generated public C interfaces. The
starter manifest therefore retains interface epoch `1` and assembly schema `2`,
with the existing template file hashes. Selected provider cold initialization
and cleanup now execute in C through static direct calls. Factory lookup remains
free of hardware effects, and failed cleanup retains its recovery responsibility.
The source pin records implementation identity; complete local software execution
requires fresh evidence for this pin, and physical HIL remains `not_executed`.

当前平台采用类型化配置 IR 版本 `2`，对外生成的 resolved JSON 投影仍为版本 `1`。
Python resolver 返回不可变的类型化对象；本示例通过 TOML 命令行入口和生成的公共
C 接口接入。因此，starter manifest 保持接口版本 `1`、装配版本 `2` 和现有模板
文件哈希。选定 provider 的冷态初始化与清理已通过静态直接调用交由 C 实现；
factory 查找不产生硬件效果，清理失败仍保留恢复责任。源码 pin 记录实现身份，
完整本地软件验证需要对应此 pin 的新执行证据；实板 HIL 保持 `not_executed`。

The maintained matrix has one Native configuration and eight exact software
reference configurations with two backends each: seventeen presets in total.
H743 and U575 software references use `stm32-h743-nucleo-*` and
`stm32-u575-nucleo-*`; each links `platform_reference` and `uart_owner_runtime`.
Both applications consume generated public faces. The logical `uart1` instance
maps to H743 USART3 or U575 USART1 through the authored Board binding, without
exposing vendor types or deriving hardware indices in application code.

`reproduction/assemblies/*.toml` selects one Board package, clock profile, OS backend,
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

The reference application opts into caller-owned cold diagnostics:

```c
nx_platform_diagnostic_t diagnostic;
nx_platform_start_result_t startup =
    nx_platform_start_diagnosed(&diagnostic);
```

The ordinary `startup.primary`, `startup.cleanup` and
`startup.remaining_effects` retain their authority. The additional `phase` and
`controller_index` identify the failed startup stage; `cleanup_phase` and
`cleanup_controller_index` independently identify failed rollback. Indices are
zero-based positions in this exact assembly's `resolved.json` `controllers`
array, not typed factory IDs, physical peripheral numbers or stable identities
across configurations. `SIZE_MAX` means that no controller applies. Successful
calls clear the diagnostic. The pointer is never retained, and a caller can keep
a copied failure snapshot while a later cleanup attempt writes another output.

`nx_platform_stop_diagnosed(&diagnostic)` returns the normal stop result. Its
metadata does not invent a remaining-effect count or prove a pending request
was released. BUSY, IO and QUARANTINED still require preserving the actual
request, buffers, notification targets and provider ownership; the execution
owner must continue the permitted service or explicit recovery. A rejected
second start does not revoke the first owner's leases even when that rejected
call has no additional remaining effects.

Native failure reporting prints these actual fields to stderr. MCU composition
does not acquire a logger, worker, HAL handle or recovery policy to provide the
diagnostics. The product decides how to record and recover from a cold failure.
`reference_lifecycle_google` executes the real generated Native platform: it
checks success reset, masked-context rejection, repeated-start ownership and an
accepted UART request whose held drain keeps stop BUSY until service can settle
the cancellation. This is a host ownership test, not physical DMA qualification.

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
The dependency identity requires fourteen maintained top-level SDK submodules
and their recursive children, including both reviewed H7 and U5 device closures
and the selected official STM32F4 HAL/LL package. Vendor types remain private to
the SDK provider; applications continue to use the same public typed faces.
Fresh Native Google execution includes the public event/byte receive policy;
CI retains its discovery and XML separately from lifecycle and fault policy.
The separate `tests/lifecycle-google-execution/` retains cold Platform discovery,
XML and execution commands; it is required by the commit gate and complete
Native CTest catalogue.
