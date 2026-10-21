# Current validation scope

This page preserves the 2026-10-12 software qualification and its original
evidence identities. The current SDK pin is selected by
`dependencies/nexus.lock.json` and the matching Gitlink. Required fresh integration
is described below; [release integration](release-0.1.0.md) preserves the original
0.1.0 snapshot. The historical counts,
archives and GitHub runs below do not qualify that new SDK source.

The current architecture replaces the previous HAL/OSAL/Runtime and eight-app
matrix. Old counts, CI runs and `evidence/platform-refactor-validation.json`
remain historical; they do not qualify this implementation.

The [2026-10-12 result record](soc-refinement-results.json) binds clean software
execution to examples code commit `3de8523811b00da8bd887abf53e61eedbaf5161e`
and pinned Nexus SDK `98270512ea2ff7eefe8ff79720018e582be3a949`. Both exact
trees and before/after source hashes are recorded there. All eight Native CTests,
43 GoogleTest cases across three programs, 44 Python tooling tests and twenty
ARM ELF budget checks passed without failures or skips. The twenty images cover
five boards, two backends and both MCU application targets.

Native/Google/tool execution comes from the successful normal commit hook for
the exact qualified tree. The formal runner checked and attached that raw
evidence, then executed the ARM and HIL preparation matrix. Its 46 commands
returned zero; source and SDK identities remained unchanged. Documentation
updates record this code snapshot and do not qualify a subsequent documentation
HEAD or imply a separate GitHub Actions result.
The earlier `8cbc6e49` / SDK `646a331c`, `f02b9186` / SDK
`3728b334` and `1f4116ae` / SDK
`23cde7e5` reports remain historical. Their passes and invocation failures do
not qualify this SDK update. The current matrix was executed again after the
new pin passed its normal commit hook.

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
3. Five actual finite Native applications under ASan/UBSan and all five complete
   Google targets: owner lifecycle, fault policy, reference RX, cold platform
   lifecycle and UART FIFO acquisition. All ten CTests require fresh nonempty
   JUnit and unfiltered discovery/identity comparison, with no skipped cases.
4. Thirty-two real ARM linked images: `platform_reference.elf` and
   `uart_owner_runtime.elf` for eight exact references by two OS backends. Each
   checks reset
   vector, resolved configuration digest, loaded
   Flash, address footprint, distinct RAM domains, MSP and zero libc heap against
   assembly budgets. Static reserved bytes are not measured dynamic high-water.

The build helper records commands and exits. CI is configured to retain raw logs, JUnit, source
identity, compile commands, effective configuration, ELF/BIN/MAP and resource
reports. This execution retains its raw evidence outside the Git repository at
`/dev/shm/nexus-soc-final-98270512/verification/examples/`.
The report SHA-256 is
`84c884b6b14283b53f9b4cba440ac4be8c3f26ad95332c0172ac88c622b9e787`.
Its artifact index includes eleven executed archives (Native plus ten ARM) and
ten preserved prior ARM archives; prior archives are not new qualification.
The archives include ELF/BIN/MAP, configuration, compile metadata and original
execution logs with per-file hashes. This local archive location is not a
published GitHub artifact URL. A verified durable copy is retained at
`/workspace/nexus-soc-delivery-98270512/examples-evidence.tar.gz`
(SHA-256 `c7f6f4824c85142b86ff43a428d88fb680f8725fae9e8b7f0fd57d1bdf70db9c`).
Resolve the original execution directory beneath its `examples/` prefix;
original reports retain their recorded paths and bytes. Reports bind actual
inputs and outputs; an older successful report cannot qualify changed behavior.

Joint development in `build/nextgen-development/` is deliberately uncommitted
and `publishable=false`. It is useful implementation feedback, not a sealed
release result. The formal dependency checker has no dirty-source bypass.

Historical preparation against `6fc1ed75` failed at its missing public
`nexus/core/fault.h`; that raw failure and its partial applicable execution stay
retained. It no longer describes the formal lock, whose exact identity is in
`dependencies/nexus.lock.json`. See [delivery preparation](delivery-preparation.md)
for the historical scope. H743/U575 integration and the new receive policy were
qualified by the fresh named code snapshot and complete matrix above; neither
inherits qualification from that earlier preparation.

All physical timing, interrupt-load, oscillator/reset, connector/electrical,
Flash power-cut and watchdog behavior remains **not executed**. A Native model
or ARM image cannot establish those qualifications. The SDK HIL tools prepare
stations and cases for a later physically connected validation run.
All ten HIL plans from that historical run remain `prepared_unbound`; its software
report retains `publishable=false` and `cycles_status=not_measured`.
