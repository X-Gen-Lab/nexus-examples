# Installed SDK reference application / 已安装 SDK 示例

This directory is a self-contained consumer project. The root examples build
uses these same application files and TOML assemblies through its pinned source
SDK. This project instead uses `find_package(Nexus CONFIG REQUIRED)`; it does
not copy or discover platform, Board or vendor implementations.

```sh
cmake -S reproduction -B build/installed-native -G Ninja \
  -DNexus_DIR=/sdk/lib/cmake/Nexus \
  -DNEXUS_ASSEMBLY_FILE="$PWD/reproduction/assemblies/native.toml"
cmake --build build/installed-native
ctest --test-dir build/installed-native --output-on-failure
```

For ARM, select one maintained assembly and pass the installed SDK's
`share/nexus/src/cmake/toolchains/arm-gcc.cmake` as `CMAKE_TOOLCHAIN_FILE`.
Outputs are `bin/nexus_firmware.elf`, `bin/nexus_firmware.bin` and
`nexus-sdk/generated/resolved.json`. Each configuration needs its own build
directory. `NEXUS_EXPECTED_SOURCE_REVISION` may bind the expected SDK commit.

The application owns its static request and its FreeRTOS task/stack. Factory
lookup is cold and has no hardware effects. After accepted TX, request and bytes
remain borrowed until settlement, including on timeout or quarantine. Service
progress remains explicit; startup does not start a scheduler. GPIO/UART roles
are authored instance IDs in each assembly, never provider-discovered aliases.

The F103, GD32F303 and G474 configurations are exact software references. Their
silicon routes do not assert an inspected PCB, connector, voltage or peripheral.
The other board configurations retain their existing documented scope. All
physical HIL remains `not_executed`; do not treat a linked image as board
qualification. The example never programs physical Flash.

For formal offline reproduction, commit this directory first. In the platform
`tools/evidence/reproduce.py --prepare-spec` invocation, pass the separate
examples checkout as `--consumer-source`, `reproduction` as
`--consumer-project`, and an assembly inside this directory as `--assembly`.
The platform SDK, consumer Git source and immutable build environment remain
separate inputs. Two fresh network-disabled builds must compare the same
ELF/BIN bytes before claiming reproduction.

本目录是独立的已安装 SDK 消费工程。仓库根构建复用这里同一份应用和 TOML，
通过锁定的源码 SDK 接入；本工程使用 `find_package`，不复制平台或厂商实现。
Native 命令见上；ARM 需选择精确配置，并传入已安装 SDK 的 ARM GCC toolchain。
每个配置使用独立构建目录；产物与 resolved 路径见上。

应用拥有静态 request、任务和栈。factory 查找不产生硬件效果；TX 被接受后，即使
超时或隔离，也必须保留 request 和字节直到 SETTLED。应用显式推进 service，
平台启动不创建调度器。角色直接来自 TOML 实例 ID，不依赖运行时发现。

F103、GD32F303、G474 仅建立精确软件参考，不推断真实 PCB 接线与电气资格。
实板 HIL 均为 `not_executed`；本示例不操作物理 Flash。正式离线复现前需提交本
目录，再声明独立 consumer 工程、平台 SDK 和不可变环境；两次全新断网构建的
ELF／BIN 逐字节一致才建立复现证据。
