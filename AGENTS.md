# Nexus examples maintenance

This repository owns public application examples and composition. Nexus owns
common platform code. Proprietary products, credentials and PCB data belong to
separate product repositories.

- Use the source SDK at `external/nexus`; update its Gitlink and
  `dependencies/nexus.lock.json` together only with actual integration evidence.
- Consume public `nexus/core`, `nexus/io`, `nexus/os`, `nexus/components` and
  generated bindings. Do not copy providers, vendor implementation or restore
  registry or implicit ownership. Use generated typed factory IDs to obtain
  readonly faces; factory lookup has no allocation or hardware side effects.
  Public types use the selected SDK ABI.
- The sole authored configuration is `reproduction/assemblies/*.toml`; CMake presets define seventeen
  maintained builds. Separate build roots prevent stale effective configuration.
- `nx_platform_start/stop` own selected infrastructure. Applications own workers,
  scheduler start, component composition, budgets and recovery. No implicit main.
- Document task/ISR context, absolute clock-domain deadlines, admission, caller
  buffer borrowing, cancellation, service progress and final settlement.
- REJECTED retains no borrow. ACCEPTED retains descriptor and bytes through
  SETTLED. QUARANTINED still retains them; timeout never revokes ownership.
- Fixed Board facts distinguish exact part/PCB/wiring from SoC capability. Do not
  infer unknown SPI/I2C/ADC/PWM/EXTI wiring or physical support from compilation.
- Preserve SDK `.clang-format`/`.editorconfig` and backslash Doxygen headers.
  Public headers own full contracts; source omits duplicate param/return tags.
  Install `python scripts/setup_dev_tools.py` in every clone. Hooks check staged
  style/text, Conventional Commits and complete Native/Google contracts. The TDD
  gate verifies the clean SDK lock and Gitlink, rejects differing unstaged or
  untracked behavior, and checks fresh full CTest and Google execution. Hooks
  never stage changes or bypass checks; CI checks all owned files independently.
- Separate Native model execution, linked ARM ELF budgets and physical HIL.
  User defers physical boards. Record unexecuted timing/electrical/reset/power-cut
  tests honestly. Do not issue automatic destructive Flash examples on equipment.
- Run meaningful tests, reject empty/stale/all-skipped JUnit, retain actual
  commands/exits, source/dependency/config/toolchain and artifact hashes.
- An explicit local SDK path never bypasses source-lock/Gitlink/clean checks.
  Uncommitted development fixtures must remain nonpublishable. Historical source
  counts or evidence cannot qualify this changed implementation.
- Conventional Commit examples: `feat(examples): ...`, `fix(uart): ...`.

Use TDD for changed behavior: capture a failing contract, implement the smallest
correct change and refactor after it passes. Nexus firmware contract development
uses the SDK repository's pinned GoogleTest/GoogleMock host framework; the target
application remains C11 and contains no host test runtime. Python tooling tests
cover configuration, invocation and evidence boundaries. A hook pass alone does
not establish the historical order of edits or physical board qualification.
