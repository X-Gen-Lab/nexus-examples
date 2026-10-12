# Delivery boundaries

Software deliverables are five explicit application examples, eleven immutable
configuration presets, direct generated bindings, SDK-locked development hooks,
source/dependency verification, finite Native execution and real MCU ELF budgets.
Platform providers remain in Nexus; product policies remain external.

The software execution recorded on 2026-10-12 passed for examples code commit
`3de8523811b00da8bd887abf53e61eedbaf5161e`, tree
`1b9b3245da8a1b5c797479d68bb5001f73f06dbf`, using the clean pinned Nexus SDK
`98270512ea2ff7eefe8ff79720018e582be3a949`, tree
`8d3e69d3179d7b0d2382c5fd074c970dfc9348d9`.

The normal commit hook executed all eight Native CTests: five finite applications
and three GoogleTest programs containing 43 cases (28 lifecycle, seven fault
policy and eight reference RX). All passed without failures or skips; the full
Python tooling suite passed 44 tests. Formal ARM validation then built both
`platform_reference.elf` and `uart_owner_runtime.elf` for every one of the ten
board/backend presets. All twenty independent ELF budget checks passed. Ten HIL
plans remain `prepared_unbound`, with physical execution `not_executed`.

[Recorded results and hashes](soc-refinement-results.json) bind the actual
reports, commands and external archives. Native evidence was attached from the
normal hook after checking its exact index tree and SDK identity; it was not
rerun or counted twice. The formal runner completed 46 commands with zero exit
codes and unchanged source/SDK snapshots. This record describes the named code
commit; a later documentation commit does not establish new execution at its HEAD.
The earlier `8cbc6e49` / SDK `646a331c`, `f02b9186` / SDK
`3728b334` and `1f4116ae` / SDK
`23cde7e5` reports remain historical. The recorded
matrix above was executed again after pinning the corrected SDK; no previous
ARM result was promoted to qualify the changed source.

H743/NUCLEO-H743ZI2 and U575/NUCLEO-U575ZI-Q add software-reference GPIO and
IRQ-byte UART compositions under baremetal and FreeRTOS. Their exact cold clock,
cache/security and Board fitting prerequisites remain part of the SDK contract.
No connected-board acceptance or advanced DMA/cache/TrustZone support follows
from these configurations. The reference RX policy rejects damaged reception
before authorizing any echo and is exercised with the pinned GoogleMock framework.

The Gitlink and source lock agree with that SDK identity. The implementation is
available in [the SoC reference integration PR](https://github.com/X-Gen-Lab/nexus-examples/pull/2).
The retained execution remains `publishable=false`: it records software
qualification and does not publish a release or establish remote CI completion.
Development snapshots and earlier failures retain their original scope.

The repository does not contain customer products, physical qualification,
production signing credentials, a shipping network stack or an established LTS
support commitment. Current physical HIL is deferred by the user. The validation
workflow publishes software evidence while preserving that boundary.

See [current validation](refactor-validation.md), [Board facts](boards.md) and
[delivery preparation](delivery-preparation.md) for the retained fault policy,
current-pin integration boundary and unbound HIL work. See also
[iteration direction](examples-roadmap.md). The older plan and evidence are
historical references, not acceptance of this changed architecture.
