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
- Use the documented two-repository refactor plan; proposed APIs are not current
  capabilities. The current Product facade is a transitional SDK dependency.
- Run meaningful integration tests, reject zero tests and record final commits.
- Never run Flash erase examples automatically on hardware or embed real keys.
