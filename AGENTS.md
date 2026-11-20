# Nexus examples maintenance

This public repository owns application examples and composition. Nexus owns
common platform code. Application source and execution artifacts stay at their
owning repository; do not copy them into the platform repository. Proprietary
products, credentials and PCB data belong to separate product repositories.

- Use the source SDK at `external/nexus`; update its Gitlink and
  `dependencies/nexus.lock.json` together only with actual integration evidence.
- Consume public `nexus/core`, `nexus/io`, `nexus/os`, `nexus/components` and
  generated bindings. Do not copy providers, vendor implementation or restore
  registry or implicit ownership. Use generated typed factory IDs to obtain
  readonly faces; factory lookup has no allocation or hardware side effects.
  Public types use the selected SDK ABI.
- Each composition owns one authored TOML. Main examples use
  `configs/*.toml`; standalone recipes and versioned starters own
  their local `assembly.toml`. Nine Native/F407 builds cover four exact F407
  Boards and both MCU backends. Retired SoC recipes remain in their historical Git snapshot. Separate
  build roots prevent stale effective configuration.
- Presets own actual build directories. The build wrapper reuses the selected
  SDK's preset reader, validates configure/build/test association before any
  compilation, and holds the actual binary directory's lock through execution
  and report validation. Do not reconstruct a directory from a preset name.
  Select the source SDK before project() so direct ARM presets use its toolchain;
  conflicting environment/cache sources reject instead of silently switching.
- `nx_platform_start/stop` own selected infrastructure. Applications own workers,
  scheduler start, component composition, budgets and recovery. No implicit main.
- Document task/ISR context, absolute clock-domain deadlines, admission, caller
  buffer borrowing, cancellation, service progress and final settlement.
- REJECTED retains no borrow. ACCEPTED retains descriptor and bytes through
  SETTLED. QUARANTINED still retains them; timeout never revokes ownership.
- Fixed Board facts distinguish exact part/PCB/wiring from SoC capability. Do not
  infer unknown SPI/I2C/ADC/PWM/EXTI wiring or physical support from compilation.
- Preserve SDK `.clang-format`/`.editorconfig` and backslash Doxygen headers.
  CMake formatting uses the selected SDK's locked gersemi, `.gersemirc` and
  shared checker; do not copy another formatter implementation into this repo.
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
correct change and refactor after it passes. Application contract development
uses this repository's pinned `external/googletest` GoogleTest/GoogleMock host
framework; the target application remains C11 and contains no host test runtime.
Installed SDK tests use its public JUnit validator, not platform development-test
helpers. Python tooling tests under `tests/tooling` cover configuration, invocation and
evidence boundaries. Original execution records retain their source identity in Git history or the
recorded artifact, outside the active source tree. A hook pass alone does
not establish the historical order of edits or physical board qualification.
