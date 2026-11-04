# Private Nexus examples delivery assets

This private branch retains the original source-bound Nexus examples evidence.
Repository visibility remains private. Read `docs-examples-delivery-20261029-gb029cc4b/final-index.json` for the exact platform and examples source identities, original package records and recovery instructions.

Run `python3 -B verify_assets.py` before committing. The ordinary pre-commit hook runs the same unchanged verifier. Each namespace file is registered with its original size and SHA-256. Evidence archives are unsigned, retain original qualification boundaries and do not establish physical HIL or product release. Authentication is required for commit-specific GitHub contents downloads.

Historical examples originally discoverable on the public platform asset branch are now also preserved privately under `docs-legacy-examples-retention-20261030/retention-index.json`. This retention does not erase public history.

Private archive storage recovery, 2026-11-03: `docs-private-cold-archive-20261103/transport.json` records an exact historical opaque archive and authenticated recovery instructions. No new P2 qualification, fresh inner-tree restore, physical HIL or product release is asserted. Fetch source main/feature/commit explicitly; do not broad-fetch this artifact branch.

Private historical storage relocation, 2026-11-04: `docs-private-six-archives-20261104/transport.json` binds six original archives and preserved historical metadata. Recovery instructions authenticate every selected raw part and reconstruct each whole archive SHA without claiming a fresh inner-tree restore, current P2 qualification, physical HIL or product release. Fetch source branches/commits explicitly; do not broad-fetch this artifact branch.
