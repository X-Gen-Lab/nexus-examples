# 构建与 SDK 消费 / Build and SDK consumption

`external/nexus` Gitlink 和 `dependencies/nexus.lock.json` 共同选择精确 SDK
commit/tree。`scripts/verify_sdk.py` 检查锁、Gitlink、干净检出和已选依赖；显式
源码路径只改变位置，不放宽身份验证。正式验证拒绝 dirty 源和 candidate 替代。

The lock and Gitlink select one exact SDK. The verifier checks the clean source
and selected dependency closure. A path override does not change that identity;
formal integration rejects dirty or candidate substitutes.

## 准备与源码构建 / Preparation and source build

使用 Linux、Python 3.12、Git、GCC/G++ 及 SDK 锁定的 CMake、Ninja、格式化工具。
先执行根 README 的依赖准备，再从仓库根运行：

Prepare dependencies explicitly as described in the root README. Use the SDK's
locked host tools; ordinary build and verification commands never fetch inputs.

格式检查复用所选 SDK 的 C／CMake 检查入口及配置；准备脚本和 hook 环境安装相同
锁定版本的 gersemi，不在提交时下载或自动改写文件。

Style checks reuse the selected SDK's C/CMake checkers and configuration. Setup
and hook environments install the same locked gersemi version; commits neither
download tools nor rewrite files.

```sh
python scripts/verify_sdk.py
python scripts/check_style.py --all
python scripts/build.py --preset native-debug --test --parallel 2
python scripts/build.py --preset stm32-qiming-baremetal --parallel 2
python scripts/build.py --preset stm32-qiming-core-freertos --parallel 2
```

`CMakePresets.json` 是主矩阵权威，`configs/*.toml` 选择硬件、后端、组件与预算。
维护预设默认使用独立的 `build/<preset>`；自定义预设以实际 `binaryDir` 为准。
脚本复用所选 SDK 的预设解析器，按实际目录加锁、保存日志和 JUnit，不另猜目录。
`--test` 在编译前检查对应 test preset 及其配置关联。ARM 的两个主目标各生成
ELF、BIN、map 和资源报告。开发构建成功不替代完整交付验证。

Presets own the build matrix and actual binary directories; TOML owns
composition. The runner reuses the selected SDK's preset reader and keeps its
lock, log and JUnit in that directory, including user presets and paths with
spaces. Missing or mismatched test presets fail before compilation. Both ARM
targets receive independent resource checks. A developer build does not qualify
the complete matrix.

已准备工具和依赖后，也可直接使用 CMake；默认 ARM 工具链来自 `external/nexus`，
不需要先设置环境变量：

After explicit tool and dependency preparation, CMake also works directly. ARM
presets use the default `external/nexus` SDK without an environment variable:

```sh
cmake --preset stm32-qiming-baremetal
cmake --build --preset stm32-qiming-baremetal --parallel 2
```

选择其他位置时，脚本使用 `--nexus-source /path/to/nexus`；直接 CMake 使用
`-DNEXUS_SOURCE:PATH=/path/to/nexus`。源码在 `project()` 前确定，工具链和平台使用
同一来源。环境变量与 CMake cache 指向不同 SDK 时明确拒绝；切换来源使用独立构建
目录。直接 CMake 不取得脚本的构建锁，调用者须保证目录独占。

Use `--nexus-source /path/to/nexus` with the runner or
`-DNEXUS_SOURCE:PATH=/path/to/nexus` with CMake. Source selection precedes
`project()`, so the toolchain and platform use the same SDK. Conflicting
environment and cache sources fail explicitly; use separate build directories
when switching SDKs. Direct CMake does not acquire the runner's build lock, so
its caller must keep the directory exclusive.

脚本在构建锁内核对预设、SDK 和 CMake 缓存。首次运行先配置工程。随后修改
应用 C 文件，直接重跑同一条构建命令即可，Ninja 只重建受影响的目标。
预设或编译器发生变化时，脚本会重新配置。已安装的 Source SDK 每次都会验证
包内容。旧 SDK 缺少增量配置接口时，脚本每次都会显式配置。

The runner checks the preset, SDK and CMake cache under the build lock. The
first build configures the project. After editing an application C file, run
the same command again. Ninja rebuilds the affected targets. Changing the
preset or compiler triggers configuration. An installed Source SDK receives
its payload check on every invocation. Earlier SDKs without the incremental
configuration API use explicit configure each time.

## 显式平台装配 / Explicit platform assembly

源码 checkout 和安装 SDK 使用同一公开 CMake API。`find_package` 检查所选 SDK
并提供函数；`nexus_add_platform` 才解析唯一 TOML、创建静态实例及平台目标。
每个镜像由 `nexus_add_firmware` 独立接入启动对象、链接布局和资源检查：

Source checkouts and installed SDKs share the same public CMake API. Discovery
checks the SDK and provides functions. Platform creation explicitly resolves one
assembly. Each firmware owns its startup objects, link layout and resource check.

```cmake
cmake_minimum_required(VERSION 3.31)
project(controller LANGUAGES C ASM)
find_package(Nexus 0.2.1 EXACT CONFIG REQUIRED)
nexus_add_platform(ASSEMBLY "${CMAKE_CURRENT_SOURCE_DIR}/assembly.toml")
nexus_add_firmware(controller SOURCES main.c LIBRARIES Nexus::Log)
```

独立模板通过 `Nexus_DIR` 选 SDK：源码为 `/path/to/nexus/cmake`，安装版为
`/path/to/sdk/lib/cmake/Nexus`。ARM 工具链须在 `project()` 前由 preset 或命令行
选定。模板不配置平台开发测试或 workload，也不进入平台根开发工程。
本仓根入口保留 `NEXUS_SOURCE` 供预设与构建脚本选择同一工具链和 SDK，并将
`NEXUS_ASSEMBLY_FILE` 明确传给平台函数；它是本工程的输入，不是 SDK 的隐式开关。

For copied starters, set `Nexus_DIR` to `/path/to/nexus/cmake` for source or
`/path/to/sdk/lib/cmake/Nexus` for an installed SDK. Select the ARM toolchain
before `project()` through the preset or command line. Starters do not configure
SDK development tests or workloads. This repository keeps `NEXUS_SOURCE` for
consistent source/toolchain selection and explicitly forwards its preset assembly.

## 编写应用配置 / Authoring application configuration

从对应 Board 的模板开始，不需要阅读 SoC 驱动来猜字段。`console` 是应用角色名，
`binding = "uart1"` 来自板卡绑定；波特率、容量和优先级由应用显式选择，再由完整
配置校验器检查。以已有启明 V3.1 配置为例，从仓库根执行：

Start from the matching Board template. Application roles are authored names;
bindings come from the Board. Select baud, capacity and priority explicitly,
then validate the complete composition. This example uses the maintained V3.1:

```sh
python external/nexus/tools/configure/configure.py list-bindings \
  --board stm32f407_qiming_v31 --kind uart
python external/nexus/tools/configure/configure.py check \
  --assembly configs/qiming-baremetal.toml
python external/nexus/tools/configure/configure.py explain \
  --assembly configs/qiming-baremetal.toml
```

查询给出候选 binding、规范模式和资源；它不替代完整配置接纳。模板中的
`mode = "irq"` 与 `rx.format = "events"` 会解析为查询显示的 `irq-byte-event`。
`explain` 展示最终接口、引脚、资源、容量和 factory 获取方式。TOML 的
`uart.console` 对应 `nx_factory_uart_ref(NX_UART_REF_CONSOLE)`，业务组件接收该
公共接口，不读取配置文件。不要修改 generated 目录。

The binding query lists candidates. `mode = "irq"` plus `rx.format = "events"`
resolves to the canonical `irq-byte-event` mode. `explain` shows effective
interfaces, resources and factory access. Components receive the public port;
they neither parse TOML nor depend on generated factory headers.

仓库及五个独立模板提供 `.taplo.toml`，供支持 Taplo 的编辑器关联所锁定 SDK
提交的 schema；只匹配应用装配 TOML，不把工具自身的 TOML 当作装配。首次远端
schema 读取需要联网，后续缓存由编辑器管理。离线编辑可将 `schema.path` 改为
本地所选 SDK 的 `tools/configure/assembly.schema.json`。切换 SDK 时同步更新
关联；本地 `check`、构建和 CI 始终使用实际 SDK，不联网读取编辑器 schema。

The checkout and copied starters include an optional Taplo association pinned
to the locked SDK commit. The initial schema fetch needs network access; editor
caching is optional. For offline editing, point `schema.path` at the selected
local SDK's schema. Update it with the SDK. Local checks and builds use the
actual SDK and never fetch this editor schema. The editor uses the documented
[Taplo configuration format](https://taplo.tamasfe.dev/configuration/file.html).

schema 补全只解释字段，不代表任一芯片或 Board 支持该模式。不存在的绑定需要
维护正确的 Board 事实，不能凭 MCU 有该外设就自行填写引脚。32 项 UART event
容量是 32 个事件；当前 ARM 布局每项 16 B，数组为 512 B，不是 32 字节。

Schema completion describes fields, not hardware support. Missing bindings need
reviewed Board facts. UART event capacity counts events: 32 entries occupy 512 B
under the current ARM layout, independently of other state and alignment.

## 已安装 SDK / Installed SDK

`tests/integration/installed_sdk` 使用 `find_package(Nexus)`，复用 `apps/` 中同一份
C11 应用，不复制平台或 provider。该入口属于完整例库，不能仅复制这个子目录使用；
独立复制工程使用 `templates/`。

The installed integration project reuses application sources from this checkout.
It has no source-SDK fallback. Use a starter for a separately copied project.

先从干净、已提交且匹配源码锁的 SDK 导出完整 Native profile，输出放在 SDK
输入检出之外：

Export from the clean committed SDK selected by the lock. Keep the complete
prefix outside its input checkout; the profile selects only required payloads.

```sh
python external/nexus/cmake/package/package_source_sdk.py \
  --source "$PWD/external/nexus" --output /path/to/native-sdk \
  --host-support --component network --component tls --component mqtt
cmake -S tests/integration/installed_sdk -B build/installed-native -G Ninja \
  -DNexus_DIR=/path/to/native-sdk/lib/cmake/Nexus \
  -DNEXUS_EXPECTED_SOURCE_REVISION=<full-locked-commit> \
  -DNEXUS_ASSEMBLY_FILE="$PWD/configs/native.toml" \
  -DNEXUS_TEST_SUPPORT=ON -DNEXUS_ENABLE_SANITIZERS=ON
cmake --build build/installed-native --parallel 2
ctest --test-dir build/installed-native --output-on-failure --no-tests=error
```

手工命令用于理解消费入口；正式本地 CI 负责 fresh JUnit、源码/包身份及消费后完整
校验。当前默认 installed 执行范围为两个应用 CTest；编译网关目标不表示已运行
网关全部场景。ARM 消费另传安装 prefix 内的 `cmake/toolchains/arm-gcc.cmake`。

Local CI adds fresh-report and identity checks, then revalidates the complete
package after consumption. Default installed execution has two application
CTests; compiled targets do not imply every scenario ran. ARM uses the installed
SDK's own toolchain file and a separate build directory.

## 完整本地 CI / Complete local CI

显式工具链安装器校验 SHA-256 锁定的归档和成员，可在准备时下载。每次执行选择新
输出目录；缺工具、旧报告、跳过测试、源码变化或失败命令都不能补成成功。

The locked ARM installer verifies archive and installed members. Use a new output
directory for each run; the runner retains failures and actual command results.

完整 Native 工具回归也配置默认 ARM preset，因此先准备锁定编译器并将其加入
`PATH`。托管 Native 作业执行同一步骤；后续 ARM 矩阵复用相同锁定归档缓存，
安装器仍校验归档与安装成员。

The complete Native tool suite also configures the default ARM preset. Prepare
the locked compiler and add it to `PATH` first. Hosted Native CI does the same;
the subsequent ARM matrix reuses the same locked archive cache, with archive
and installed-member verification on every use.

```sh
python external/nexus/scripts/ci/install_arm_toolchain.py \
  --install-dir "$PWD/.tools/arm" \
  --cache-dir "$PWD/.tools/arm-archives"
export PATH="$PWD/.tools/arm/bin:$PATH"
python scripts/local_ci.py --parallel 2 \
  --arm-toolchain "$PWD/.tools/arm" \
  --toolchain-cache "$PWD/.tools/arm-archives" \
  --output "$PWD/build/local-ci-001"
```

可用 `--installed-sdk /path/to/sdk` 复用相同 commit/tree 的完整正式包，仍执行
前后校验。`--scope native` 或 `--scope arm --preset <name>` 仅为部分验证。
本地与 GitHub workflow 调用同一软件入口；本地执行不写成托管 CI 成功。

A matching complete SDK may be reused with `--installed-sdk`; integrity checks
still run. Selected scopes/presets remain partial results. Hosted workflows reuse
the same software entry point; local execution is labelled local.

## 工程模板 / Standalone starters

五个模板均自带 CMake、TOML、C11 入口、README 和可选编辑器关联。平台 create
工具从例库的完整提交读取清单与摘要，不联网获取 SDK，也不复制 `.git` 或平台树：

Each starter is independently copyable. Creation reads committed template
objects and records provenance; it does not acquire a platform checkout.

```sh
python external/nexus/tools/dev/dev.py create \
  --templates "$PWD" --revision <full-examples-commit> \
  --template native --output /path/to/new-product
cmake -S /path/to/new-product -B /path/to/product-build -G Ninja \
  -DNexus_DIR=/path/to/native-sdk/lib/cmake/Nexus
cmake --build /path/to/product-build --parallel 2
ctest --test-dir /path/to/product-build --output-on-failure --no-tests=error
```

MCU 模板另选对应 Board 并传 SDK ARM toolchain。硬件装配、驱动支持、ARM 链接和
实板结果是不同事实，见[板卡指南](boards.md)与[验证规则](validation.md)。

Choose an exact Board and ARM toolchain for an MCU starter. Board fitting,
software support, linking and physical results are separate facts.
