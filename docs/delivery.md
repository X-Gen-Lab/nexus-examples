# Delivery boundaries

Software deliverables are five explicit application examples, eleven immutable
configuration presets, direct generated bindings, SDK-locked development hooks,
source/dependency verification, finite Native execution and real MCU ELF budgets.
Platform providers remain in Nexus; product policies remain external.

The current source registers five finite application CTests and three complete
GoogleTest execution targets. Each of the ten ARM presets produces both
`platform_reference.elf` and `uart_owner_runtime.elf`, with independent budgets.
These are required validation scopes, not proof that this changed source has
already passed against the currently pinned SDK.

H743/NUCLEO-H743ZI2 and U575/NUCLEO-U575ZI-Q add software-reference GPIO and
IRQ-byte UART compositions under baremetal and FreeRTOS. Their exact cold clock,
cache/security and Board fitting prerequisites remain part of the SDK contract.
No connected-board acceptance or advanced DMA/cache/TrustZone support follows
from these configurations. The reference RX policy rejects damaged reception
before authorizing any echo and is exercised with the pinned GoogleMock framework.

The integration branch must be committed and the SDK Gitlink/source lock updated
together before final clean validation. Root coordination owns those pins and
incremental commits. Development builds of uncommitted sources are marked
nonpublishable and cannot be promoted as final release artifacts.

The repository does not contain customer products, physical qualification,
production signing credentials, a shipping network stack or an established LTS
support commitment. Current physical HIL is deferred by the user. The validation
workflow publishes software evidence while preserving that boundary.

See [current validation](refactor-validation.md), [Board facts](boards.md) and
[delivery preparation](delivery-preparation.md) for the retained fault policy,
current-pin integration boundary and unbound HIL work. See also
[iteration direction](examples-roadmap.md). The older plan and evidence are
historical references, not acceptance of this changed architecture.
