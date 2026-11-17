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

```sh
python scripts/verify_sdk.py
python scripts/check_style.py --all
python scripts/build.py --preset native-debug --test --parallel 2
python scripts/build.py --preset stm32-qiming-baremetal --parallel 2
python scripts/build.py --preset stm32-qiming-core-freertos --parallel 2
```

`CMakePresets.json` 是主矩阵权威，`configs/*.toml` 选择硬件、后端、组件与预算。
每个 preset 使用独立的 `build/<preset>`。ARM 的两个主目标各生成 ELF、BIN、map
和资源报告。开发构建成功不替代完整交付验证。

Presets own the build matrix and distinct build roots; TOML owns composition.
Both ARM application targets receive independent resource checks. A developer
build alone does not qualify the complete matrix.

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

```sh
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
