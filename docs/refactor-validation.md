# Current validation scope

The current architecture replaces the previous HAL/OSAL/Runtime and eight-app
matrix. Old counts, CI runs and `evidence/platform-refactor-validation.json`
remain historical; they do not qualify this implementation.

Original JSON reports beneath the exact historical roots
`evidence/reports/a4d5ac6/` and `evidence/reports/5498b2b/` are preserved
byte-for-byte, including their original whitespace and newline conventions.
They are raw records excluded from source formatting, not a style-debt baseline
or acceptance of the new platform. Current evidence, nested lookalike paths and
C sources remain subject to the normal checks.

Current required software checks are:

1. Exact formal SDK commit/tree, example Gitlink, recursive maintained source
   dependencies and reviewed GD32 import; a clean checkout is mandatory.
2. SDK-locked format/comment/text tools and Conventional Commit hooks; complete
   CI checks plus real formatter/boundary regression tests.
3. Five actual finite Native applications under ASan/UBSan and three complete
   lifecycle/fault-policy/reference-RX Google targets: eight CTests with fresh nonempty JUnit
   and unfiltered discovery/identity comparison; no skipped cases.
4. Twenty real ARM linked images: `platform_reference.elf` and
   `uart_owner_runtime.elf` for five boards by two OS backends. Each checks reset
   vector, resolved configuration digest, loaded
   Flash, address footprint, distinct RAM domains, MSP and zero libc heap against
   assembly budgets. Static reserved bytes are not measured dynamic high-water.

The build helper records commands and exits. CI retains raw logs, JUnit, source
identity, compile commands, effective configuration, ELF/BIN/MAP and resource
reports. Reports bind actual input/artifact hashes; a successful old report is
not accepted as current execution. Final clean pinned results belong to the
release evidence after coordinated source commits and pins.

Joint development in `build/nextgen-development/` is deliberately uncommitted
and `publishable=false`. It is useful implementation feedback, not a sealed
release result. The formal dependency checker has no dirty-source bypass.

Historical preparation against `6fc1ed75` failed at its missing public
`nexus/core/fault.h`; that raw failure and its partial applicable execution stay
retained. It no longer describes the formal lock, whose exact identity is in
`dependencies/nexus.lock.json`. See [delivery preparation](delivery-preparation.md)
for the historical scope. H743/U575 integration and the new receive-policy
development evidence require a fresh coordinated clean SDK pin and the complete
current matrix; neither inherits qualification from that earlier preparation.

All physical timing, interrupt-load, oscillator/reset, connector/electrical,
Flash power-cut and watchdog behavior remains **not executed**. A Native model
or ARM image cannot establish those qualifications. The SDK HIL tools prepare
stations and cases for a later physically connected validation run.
