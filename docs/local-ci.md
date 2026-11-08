# Local CI / 本地 CI

`scripts/local_ci.py` runs the maintained software checks without GitHub Actions
minutes. It uses the same CMake presets, standalone CMake projects, formal source
identity verifier, complete TDD gate, SDK packager, ELF budget checker and HIL
preparation tool. There is no alternate target graph or hardware execution.

`scripts/local_ci.py` 不消耗 GitHub Actions 额度。它调用现有 preset、独立 CMake
项目、正式源码身份检查、完整 TDD 门禁、SDK 打包、ELF 预算与 HIL 准备工具，
没有第二套构建图，也不连接、烧写或运行设备。正式交付运行要求例库行为文件与
index 一致、平台源码干净，lock 与 Gitlink 对应同一完整 commit/tree。

## Prepare and execute / 准备与运行

Use Linux x86_64, Python 3.12, Git, GCC/G++, the pinned CMake/Ninja and formatter.
Install host dependencies explicitly; the runner never calls apt or pip. The SDK
`doctor --target tools` checks the locked Python minimum and build tool versions.
The SDK and its selected dependency closure must be prepared. A relocated source
checkout is supported with exactly the same lock, clean tree and Gitlink checks.

使用 Linux x86_64、Python 3.12、Git、GCC/G++ 及锁定的 CMake/Ninja/格式化工具。
先显式安装宿主依赖；本地 CI 不自动运行 apt 或 pip。工具版本由 SDK doctor 检查，
编译器实际版本保存在日志。平台依赖必须完整，显式源码路径不放宽正式身份验证。

```sh
git submodule sync -- external/nexus
git submodule update --init -- external/nexus
python3 external/nexus/tools/dev/dev.py prepare --scope native --allow-network
python3 scripts/setup_dev_tools.py
.venv/bin/python -m pip install -r external/nexus/dependencies/environment-tools.txt
. .venv/bin/activate

# Complete software matrix; choose a NEW ignored output directory for each run.
python scripts/local_ci.py --parallel 1 \
  --nexus-source external/nexus \
  --arm-toolchain "$PWD/.tools/arm" \
  --toolchain-cache "$PWD/.tools/arm-archives" \
  --output "$PWD/build/local-ci-001"
```

Update the outer Nexus checkout before preparing its recursive dependencies.
The explicit network-enabled preparation checks existing work and origins before
synchronizing reviewed fetch locations; it preserves unknown local changes.
Ordinary hooks and local CI reuse the same preparation contract offline. They
reject missing objects, wrong origins or unverified results without fetching
from an upstream fallback or moving selected Gitlinks.

先更新外层 Nexus 检出，再准备其递归依赖。显式允许联网的准备命令会先检查
现有工作区和来源，再同步审核过的获取地址；不会覆盖未知本地修改。普通提交门禁
和本地 CI 离线复用同一合同，缺少对象、来源错误或未验证结果均失败，不自动回退
官方地址，也不移动选定 Gitlink。

The explicit `--arm-toolchain` invokes the SDK's SHA-256 locked GNU installer.
It may download its reviewed archive. Every reuse verifies the archive and the
installed members; a version string or marker alone is insufficient. Allow disk
space for extraction and verification as well as the builds. Existing preset
builds can be reused only when their cached compilers match this installation.
An incompatible cache fails with the precise build directory to remove.

`--arm-toolchain` 显式调用 SDK 的 GNU 工具链锁定安装器，可下载已审核归档；复用时
仍校验归档及安装文件，不以版本字符串或 marker 代替。请为解包和构建保留空间。
已有 preset 可增量构建，但缓存编译器必须来自同一已验证安装，冲突时明确失败。

```sh
# Focused runs are explicitly partial, never a complete matrix qualification.
python scripts/local_ci.py --scope native --parallel 1 \
  --output build/local-ci-native-001
python scripts/local_ci.py --scope arm --preset stm32-qiming-baremetal \
  --parallel 1 --arm-toolchain "$PWD/.tools/arm" \
  --toolchain-cache "$PWD/.tools/arm-archives" --output build/local-ci-arm-001
```

By default the Native stage packages a fresh publishable SDK. To reuse a complete
SDK of the same exact source commit/tree, add `--installed-sdk /path/to/sdk`.
The package verifier checks it again. The independent `reproduction` consumer
uses `find_package(Nexus)` and `NEXUS_EXPECTED_SOURCE_REVISION`, sanitizers and a
fresh CTest report, without `NEXUS_SOURCE` fallback. A changed pin needs a matching
SDK; a development fixture or incomplete package fails. All child processes set
`PYTHONDONTWRITEBYTECODE=1`. After the consumer finishes, the complete SDK verifier
runs again and the manifest identity must remain unchanged; an added bytecode
file or other package mutation fails even if the consumer tests passed.

Native 默认生成正式 SDK。可用 `--installed-sdk` 复用同一 commit/tree 的完整包，
仍执行包校验。独立 `reproduction` 使用 find_package、精确预期 revision、sanitizer
和新鲜 CTest；没有源码回退。更新 pin 后必须重新提供匹配 SDK，开发包不能替代。
所有子进程禁止生成 Python 字节码；消费者结束后再次完整校验 SDK 和原 manifest
身份。即使应用测试通过，安装包中新增 pyc 或其他变化仍会让整次验证失败。

## Workflow coverage / 工作流覆盖

The workflow and local runner cover one Native and eight F407 ARM presets.
Only complete execution qualifies that matrix; selected scopes remain partial.

工作流与本地 runner 覆盖一个 Native 和八个 F407 ARM preset。全部完成才是完整矩阵；
部分 scope 或 preset 始终明确标记为部分结果。

| Hosted step group | Local execution / 本地执行 |
| --- | --- |
| Checkout, candidate validation and candidate checkout; both jobs | Formal source verifier checks lock, Gitlink, tree and recursive dependencies before/after. Local release runs have no candidate override; candidate experiments remain separate. 正式验证不移动分支、不自动下载源码。 |
| Setup Python and prepare host dependencies; both jobs | Explicit preparation above; required commands, SDK doctor and actual version logs. No claim of fresh ubuntu-latest/apt provisioning. 显式准备并保留实际版本，不伪称执行托管环境初始化。 |
| Verify source identity; both jobs | `test_repository.py --nexus-source`, before and after all selected execution. |
| Native style, tool tests, Native build/execution | `check_style.py --all`; one `tdd_gate.py --all` owns all Python, complete Native CTest and Google discovery/execution. No filtered or skipped run qualifies. |
| SDK package, installed configure/build/execute | Original packager or complete package verification; independent sanitized CMake/CTest; SDK JUnit validator rejects missing, empty, skipped or stale XML. |
| ARM toolchain and main firmware | Original locked installer; `build.py` for every selected preset; bounded `--parallel`, default 1. |
| Standalone recipes and starters | Shared SPI runtime and the Qiming V3.1, Sky, Qiming core and Qiming V4.1 starters: five maintained consumers. 共享 SPI 运行例与启明 V3.1、天空星、启明核心板、启明 V4.1 启动模板，共五个当前维护的独立消费者。 |
| Main/owner ELF budget checks | Existing resource checker for each actual ELF and its own generated resolution/budget. Standalone ELFs are also checked. |
| HIL plan preparation | Original prepare tool, eight reviewed F407 board/backend plans; no retired Board fallback. `prepared_unbound` / `not_executed` only. |
| Artifact upload; both jobs | Local retained logs, command receipts, existing build artifacts and run summary; no hosted upload or remote status is claimed. |

## Results and failures / 结果与失败

The new output directory contains `execution.json`, actual command `.log` and
`.command.json` files, source identity snapshots, style result, standalone builds,
budgets and unbound plans. Native source builds retain their established evidence
under `build/native-debug/tdd` and `tests/*execution`; main ARM outputs remain in
`build/<preset>`. `execution.json` references that evidence. Archive the run
directory **and** referenced build reports/binaries before another run replaces
warm-build output. The exit code and summary must both indicate success.

输出目录记录真实命令、退出码、日志、身份、预算和未绑定 HIL 计划；Native/ARM
主构建沿用既有目录。下一次执行会更新 warm build 内报告，因此保留交付证据时应
同时归档运行目录及其引用的构建报告/二进制。已有输出目录、缺工具、失败命令、
身份变化、空/旧/跳过测试报告均失败，不读取历史成功结果补齐。部分运行的成功
不能替代完整矩阵；本地成功也不是 GitHub 托管检查或物理 HIL 成功。

All four F407 Boards remain disconnected. See [Board facts](boards.md) and
[HIL preparation](delivery-preparation.md#hil-readiness) before selecting any
station. Never fill an observed chip UID, probe serial or wiring field from a
software reference. The runner has no station binding or acquisition option.

四块 F407 Board 尚未接入。芯片 UID、探针编号、接线等必须来自实际观测，不能由软件参考
补造。本入口仅准备计划，没有绑定 station、采集、自动烧写或破坏性测试选项。

To inspect one prepared plan after a successful source build, the existing
read-only preparation command is sufficient:

源构建通过后，可用原有工具检查单个未绑定计划，无需连接设备：

```sh
python external/nexus/tools/hil/prepare.py \
  --fixture external/nexus/tools/hil/fixtures/stm32f407_qiming_v31.json \
  --resolved build/stm32-qiming-baremetal/nexus/generated/resolved.json \
  --report build/qiming-hil-preparation.json
```

The four mappings are Qiming V3.1 → `stm32f407_qiming_v31`, Sky →
`stm32f407ve_sky_qingchun`, Qiming core → `stm32f407zg_qiming_core`, and
Qiming V4.1 → `stm32f407zg_qiming_v41`. A Sky fixture
paired with a Qiming resolution is rejected; unknown presets and `--station`
on `local_ci.py` are rejected rather than choosing a fallback. Future physical
admission uses the SDK's existing `tools/hil/admit.py` and observed station
schema, after the equipment and workload have actually been reviewed.

四板 fixture 必须对应上列准确身份。天空星 fixture 配启明 resolved 会拒绝；
未知 preset 或向 local_ci 传入 --station 也会拒绝，不静默替换。将来接入实板后，
使用原有 admit.py 和真实观测 station schema；先审核实际设备及负载，再执行采集。
