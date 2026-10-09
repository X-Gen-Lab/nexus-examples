# 独立仓库接入 Nexus

本仓库通过 Git submodule 固定 Nexus 源码 SDK，应用代码和参考案例由本仓库维护。
唯一依赖身份记录是 `dependencies/nexus.lock.json`，其中固定仓库 URL、commit 和 tree。
生产产品应在自己的仓库维护业务流程、产品标识、私有配置、发布策略和验证证据。

## 唯一构建入口

`CMakePresets.json` 定义平台、配置片段、编译模式和工具链；普通 CMake targets
定义应用。`scripts/build.py` 只转发 CMake/CTest 命令、记录输出和返回退出码，
不解析 Kconfig，也不实现第二套构建规则。

环境要求：Python 3.11、`kconfiglib==14.1.0`、CMake 3.21 及以上、Ninja 和宿主 C 编译器。
启用 Native 安全服务时还需要 OpenSSL 开发包。当前 CI 在 Linux 执行。

```sh
git submodule update --init --recursive external/nexus
python3 -m pip install kconfiglib==14.1.0
python3 scripts/test_repository.py
python3 scripts/build.py --preset native-debug --test
```

`test_repository.py` 读取依赖锁，核验父仓库的 Nexus Gitlink 和仓库 URL、
SDK checkout 的 commit、tree 及干净状态、九组 CMake 预设的引用关系、
首发平台所需子模块的递归版本，以及 GD32 SDK 的实际文件摘要。
它不建立应用执行或硬件验证结论。`--test` 调用匹配的 CTest preset，
设置 `--no-tests=error` 并保存 JUnit 报告；没有测试不能被当作通过。

本地可明确选择已有 SDK checkout：

```sh
python3 scripts/test_repository.py --nexus-source /workspace/nexus
python3 scripts/build.py --preset native-debug --nexus-source /workspace/nexus --test
```

依赖核验仍要求指定 checkout 与依赖锁的 commit 和 tree 一致，父仓库 Gitlink 也必须匹配。
报告中的 `source_override` 只表示使用了不同位置的 checkout，不跳过版本或 Gitlink 核验。
`build.py` 本身允许指定开发中的 SDK，便于联合调试；CI 在构建前执行固定版本核验。
更新依赖时一起评审 Gitlink 和依赖锁，并重新验证全部参考配置；脚本不保存第二份版本常量。

## CMake 接入边界

父工程在 `project()` 前通过 preset 选择工具链，然后调用
`add_subdirectory("${NEXUS_SOURCE}" nexus)`。SDK 返回后使用当前实际提供的
`nexus_add_application(TARGET ... SOURCES ... EXTRA_DEPS ...)`，由它组合启动文件、
设备注册、HAL/OSAL 和选定平台。应用源码使用 Nexus 公共类型和公开 targets。

脚本同时设置 `NEXUS_SOURCE` 环境变量和 CMake 路径缓存。
ARM preset 的 `toolchainFile` 使用 `$env{NEXUS_SOURCE}/cmake/toolchains/arm-gcc.cmake`，
ARM 配置片段也从该显式 SDK 路径选择，Native 使用本仓库的
`configs/native/platform.conf`。这样本地覆盖不会继续读取另一份 checkout 的工具链。
直接运行 CMake 时需要先设置 `NEXUS_SOURCE`，例如：

```sh
export NEXUS_SOURCE="$PWD/external/nexus"
cmake --preset native-debug -DNEXUS_SOURCE:PATH="$NEXUS_SOURCE"
cmake --build --preset native-debug --parallel 4
ctest --preset native-debug --output-on-failure --no-tests=error
```

每个 preset 的父构建目录为 `build/<preset>`，SDK 子构建目录为 `build/<preset>/nexus`。
当前 SDK helper 将应用可执行文件、ELF、MAP、BIN 和 HEX 放在后者的 `bin` 中；
有效配置文件位于后者的 `generated` 中。父目录保存编译数据库、构建日志和验证报告。
不同板卡、后端和模式使用不同构建目录，同一构建只选择一个平台及有效配置。
SDK 开发测试与内置示例在子工程中关闭，本仓库只构建自身案例。

## ARM 参考配置

| 板卡 | 芯片 | 裸机 preset | FreeRTOS preset |
| --- | --- | --- | --- |
| STM32F4 Discovery | STM32F407VG | `stm32-discovery-baremetal-release` | `stm32-discovery-freertos-release` |
| 启明欣欣 V3.1 | STM32F407ZG | `stm32-qiming-baremetal-release` | `stm32-qiming-freertos-release` |
| 立创天空星青春版 | STM32F407VE | `stm32-sky-baremetal-release` | `stm32-sky-freertos-release` |
| 立创梁山派 | GD32F470ZG | `gd32-liangshan-baremetal-release` | `gd32-liangshan-freertos-release` |

在 x86_64 Linux 上准备 SDK 锁定的 ARM GNU 14.3.rel1 工具链，然后构建并检查真实 ELF：

```sh
python3 external/nexus/scripts/ci/install_arm_toolchain.py --install-dir "$PWD/.tools/arm-gnu"
export PATH="$PWD/.tools/arm-gnu/bin:$PATH"
python3 scripts/build.py --preset stm32-qiming-baremetal-release
python3 external/nexus/scripts/ci/validate_firmware_elf.py \
  --build-dir build/stm32-qiming-baremetal-release/nexus \
  --report build/stm32-qiming-baremetal-release/firmware-static-contract.json
```

工具准备脚本校验 SDK 锁定的官方分发包 SHA-256；CMake 配置不下载依赖。
ELF 校验读取真实中断向量、强符号绑定、设备注册记录、加载权限和 Flash/RAM 边界。
`--build-dir` 必须指向 SDK 子构建目录，以同时定位有效配置和固件。
报告明确记录 `hardware_verified: false`；时序、DMA、供电、引脚及掉电恢复需要实板验证。

CI 先构建并运行 Native 案例，再构建以上八组 ARM 配置并执行 ELF 静态校验。
产物保留配置、依赖身份、编译数据库、日志和相应执行/静态报告。
CI 定义存在不代表已经在远端执行成功。

## 当前 SDK 限制

当前是源码 SDK 接入，尚无安装导出的 `find_package(Nexus)` 包。
`Nexus::Product` 和 `nx_product_boot()` 是当前固定 SDK 下的过渡接入。
[平台重构计划](platform-refactor-plan.md) 的 `RF-BOOT-01`、`RF-BOOT-02`
负责保留 owner、回滚和关闭保障，拆分中性平台信息与通用 Runtime bootstrap。
计划提交 C05 在平台 API 完成后同步更新本仓库的依赖锁、Gitlink 和全部调用者，
并重跑 Native、八组 ARM 与 ELF 验证；新接口目前尚未实现。

已提供的配置片段只能使用 Nexus 当前 Kconfig schema 中的符号。
外部自定义 Board 注册、任意产品 Kconfig 扩展入口、自定义 Flash 分区和
`nexus_add_firmware` 尚未提供，本仓库使用 SDK 内置参考 Board 与布局。
外部应用可以选择已实现的服务 targets；选项关闭时相应 target 不存在，不能依赖它。

这里的依赖报告核验示例仓库及 Nexus 的源身份，不替代生产产品的多源码 SBOM、
签名信任链、制造授权或发布审批。此类产品策略应由产品仓库定义和执行。
