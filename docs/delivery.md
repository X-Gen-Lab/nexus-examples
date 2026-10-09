# Delivery boundaries

Software deliverables are four explicit application examples, seven immutable
configuration presets, direct generated bindings, SDK-locked development hooks,
source/dependency verification, finite Native execution and real MCU ELF budgets.
Platform providers remain in Nexus; product policies remain external.

The integration branch must be committed and the SDK Gitlink/source lock updated
together before final clean validation. Root coordination owns those pins and
incremental commits. Development builds of uncommitted sources are marked
nonpublishable and cannot be promoted as final release artifacts.

The repository does not contain customer products, physical qualification,
production signing credentials, a shipping network stack or an established LTS
support commitment. Current physical HIL is deferred by the user. The validation
workflow publishes software evidence while preserving that boundary.

See [current validation](refactor-validation.md), [Board facts](boards.md) and
[iteration direction](examples-roadmap.md). The older plan and evidence are
historical references, not acceptance of this changed architecture.
