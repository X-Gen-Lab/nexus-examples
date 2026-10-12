# Current validation scope

The current architecture replaces the previous HAL/OSAL/Runtime and eight-app
matrix. Old counts, CI runs and `evidence/platform-refactor-validation.json`
remain historical; they do not qualify this implementation.

The [2026-10-12 result record](soc-refinement-results.json) binds clean software
execution to examples code commit `f02b91867bb64abfa31e3c1d4b6004bd1a38b091`
and pinned Nexus SDK `3728b334b02eb2eb70bcf5d4caafbb9746837dd3`. Both exact
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
The earlier `1f4116ae` / SDK `23cde7e5` report is retained as a historical
snapshot. It does not qualify this SDK update. An initial invocation for the new
snapshot rejected a copied hook-report path before ARM execution; the successful
`formal-f02b9186-v2` invocation used the required original hook evidence. That
invocation rejection is neither a firmware regression nor TDD RED evidence.

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

The build helper records commands and exits. CI is configured to retain raw logs, JUnit, source
identity, compile commands, effective configuration, ELF/BIN/MAP and resource
reports. This execution retains its raw evidence outside the Git repository at
`/workspace/nexus-soc-qualification-20261012/examples-reference/formal-f02b9186-v2/`.
The report SHA-256 is
`46c305cefefc108e4e98bf6df26bea25b5a53edeef0e58e56be5f0b4f0f3d779`.
Its artifact index includes eleven executed archives (Native plus ten ARM) and
ten preserved prior ARM archives; prior archives are not new qualification.
The archives include ELF/BIN/MAP, configuration, compile metadata and original
execution logs with per-file hashes. This local archive location is not a
published GitHub artifact URL. Reports bind actual inputs and outputs; an older
successful report cannot qualify changed behavior.

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
All ten newly generated HIL plans remain `prepared_unbound`, and the software
report retains `publishable=false` and `cycles_status=not_measured`.
