# Nexus examples maintenance

This repository owns public application examples and their composition. Nexus
owns the reusable platform. Keep proprietary products, credentials and private
PCB data in their own repositories.

- Use the pinned source SDK at `external/nexus`; update its Gitlink and
  `dependencies/nexus.lock.json` together with actual integration evidence.
- Do not copy HAL/OSAL/vendor implementations or introduce fallback success.
- Samples must state supported boards/backends, resources and electrical setup.
- Separate Native model execution, ARM static artifact validation and real HIL.
- Keep task/ISR context, deadlines, buffer ownership and error recovery explicit.
- Platform API changes and the corresponding example caller changes require
  separate reviewable commits and compatible dependency pins.
- Presets are the build source of truth; scripts only forward CMake/CTest.
- Use the documented two-repository refactor plan and the platform execution
  record. Consume runtime/nx_runtime.h and Nexus::Firmware; bootstrap only owns
  HAL/OSAL infrastructure. Applications explicitly own workers, component and
  provider selection, Board/layout, resources and recovery policy.
- Distinguish implemented public APIs from outstanding qualification. External
  Board/layout and relocatable source-package inputs exist; their final clean
  evidence must bind the pinned SDK. MCU typed I2C and physical HIL remain
  unsupported/unverified; do not infer them from Native models or compilation.
- Run meaningful integration tests, reject zero tests and record final commits.
- Never run Flash erase examples automatically on hardware or embed real keys.
