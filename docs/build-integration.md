# 独立仓库接入 Nexus

应用组成由本仓库维护。正式源码 SDK 身份由 `external/nexus` Gitlink 与
`dependencies/nexus.lock.json` 一起固定；准确最终 pin 和软件矩阵以
[platform-refactor-validation.json](../evidence/platform-refactor-validation.json) 及
[验证记录](validation.md) 为准。当前文档描述已实现接口，不预填最终 clean 结果。

## 构建入口与身份

`CMakePresets.json` 定义平台、后端、配置、模式和工具链；普通 CMake targets 定义应用。
`scripts/build.py` 只转发 CMake／CTest 并传播退出码，不解析第二套 Kconfig。
环境为 Python 3.11、`kconfiglib==14.1.0`、CMake 3.21 以上、Ninja 和宿主 C 编译器；
ARM 使用 SDK lock 固定的 ARM GNU 14.3.rel1。只有显式启用 `CRYPTO_PROVIDER_OPENSSL`
才查找 Native OpenSSL；本仓库非加密 Config 例程不需要绑定该 provider。

```sh
git submodule update --init --recursive external/nexus
python3 -m pip install kconfiglib==14.1.0
python3 scripts/test_repository.py
python3 scripts/build.py --preset native-debug --test
```

依赖核验读取锁、Gitlink、SDK commit／tree／干净状态、必要递归依赖与 GD32 import 摘要。
它不证明应用执行或硬件结果。开发覆盖使用：

```sh
python3 scripts/build.py --preset native-debug --nexus-source /workspace/nexus --test
```

开发中的 checkout 可用于联合构建，正式身份核验仍要求匹配锁和 Gitlink。
CTest 使用 `--no-tests=error` 和实际 JUnit；空、旧或不可解析的报告不能当作通过。
本轮完整 clean 检查仍待最终证据绑定，旧 SDK 的结果不适用于新调用者。

## 父工程与应用 target

在 `project()` 前通过 preset 选择工具链，然后加入 SDK：

```cmake
cmake_minimum_required(VERSION 3.21)
project(public_example LANGUAGES C ASM)
set(NEXUS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NEXUS_BUILD_CONTRACTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${NEXUS_SOURCE}" nexus)
nexus_add_application(TARGET firmware VERSION "0.1.0"
    SOURCES main.c EXTRA_DEPS Nexus::HALDevice)
```

helper 的实际参数为 `TARGET`、`VERSION`、`SOURCES`、`EXTRA_DEPS`、`PLATFORM_DEPS`。
它链接 `Nexus::Firmware`，显式保留选定平台、SoC／Board OBJECT、startup、强 IRQ 与注册记录。
组件以 `EXTRA_DEPS` 单独选择，不自动链接全部服务。`Nexus::Config` 是有效构建配置 target，
不是 Config manager；后者使用 `Nexus::ConfigCore`／`ConfigRAM`／`ConfigFlash`。
应用首先包含 `nexus_config.h`，再包含组件公开头文件，使静态容量和组件编译配置一致。

入口使用 `runtime/nx_runtime.h` 的 `nx_runtime_bootstrap()`，失败报告及部分 ownership
遵从该公开合同；bootstrap 不创建业务任务、不启动 scheduler、不绑定 crypto provider。
FreeRTOS 应用创建自己的 worker 后调用 `osal_start()`，需要 operational 同步的组件
在调度后的任务中初始化。`nx_runtime_shutdown()` 遇到未结清设备／OSAL 对象或运行中的
MCU scheduler 返回 BUSY；不承诺 MCU kernel 热重启。身份查询见 `runtime/nx_platform_info.h`。

脚本设置 `NEXUS_SOURCE` 环境与 CMake 缓存；ARM preset 的工具链从同一路径选择。
每个父构建目录为 `build/<preset>`，SDK 子目录为 `build/<preset>/nexus`；helper 将可执行文件、
ELF／MAP／BIN／HEX 放在后者 `bin` 中，有效配置在 `generated` 中。
一份 build root 只选择一个 Board／backend／mode，不能混用不同 SDK。
`FIRMWARE_MAIN_STACK_SIZE` 是 MSP／exception linker 预留，`FIRMWARE_LIBC_HEAP_SIZE`
是 C runtime heap；它们与 OSAL task stack pool、kernel objects／heap 分开。

## 外部 Board 与 Flash layout

当前已支持显式 `-DNEXUS_BOARD_DIR=/absolute/board-package`。包提供 `board.json`、
实际 CMake 目标、受审查资源绑定与 manifest 列出的所有源码；选用 `BOARD_EXTERNAL`，
不得同时选择内置 Board。构建校验 SoC／密度／HSE、已有 controller 路由、pin／AF、IRQ、
选定 DMA 及源码路径 containment，并把真实输入摘要绑定配置和固件。
该入口扩展已维护 SoC 的 Board，不因新增 manifest 自动提供任意芯片或全部外设。

`-DNEXUS_FLASH_LAYOUT_FILE=/absolute/layout.json` 从单一输入生成 linker 和 layout 身份。
省略时 image 使用整块物理 Flash，不自动保留产品参数区。布局必须符合非均匀 erase block、
容量、权限与区域不重叠要求；当前只支持从 Flash 基址启动，非零 image offset 明确拒绝。
它不等于提供 bootloader、VTOR relocation 或可用 A/B 安装链。

typed Flash 使用 `hal/base/nx_device.h` 的 `nx_device_flash_geometry/block/region_open`，
region 具有显式 permissions 与 generation／loan 生命周期。StorageHAL 通过
`nexus/hal_flash_storage.h` 转成 StorageCore 窄端口；应用拥有 region／layout 和维护窗口。
默认例程不绑定 Flash backend，也不自动擦写。

## 可重定位源码包

当前已实现源码包与 `find_package(Nexus CONFIG REQUIRED)`，不是预编译 binary SDK。
包内包含源码、必要依赖与许可、逐文件身份；消费工程重新生成配置、编译平台及 startup／linker。

```sh
python3 -B /path/to/nexus/cmake/package/package_source_sdk.py \
  --source /path/to/nexus --output '/path/to/nexus sdk prefix'
python3 -B '/path/to/nexus sdk prefix/share/nexus/src/cmake/package/package_source_sdk.py' \
  --verify '/path/to/nexus sdk prefix/share/nexus/src'
```

```cmake
find_package(Nexus 0.1.0 EXACT CONFIG REQUIRED)
nexus_add_application(TARGET firmware SOURCES main.c)
```

通过 `Nexus_DIR=<prefix>/lib/cmake/Nexus` 选择包，并可用
`NEXUS_EXPECTED_SOURCE_REVISION` 检查完整源码 revision；ARM 在首次 `project()` 前选择
`<prefix>/share/nexus/src/cmake/toolchains/arm-gcc.cmake`。CMake helper
`nexus_package_source_sdk(OUTPUT_DIRECTORY ...)` 可准备同样的源码包。
包输出须在 checkout 外且尚不存在；路径移动后仍核验源码／依赖／导出文件身份。
开发工作树必须显式 `--development-fixture` 并在 consumer 开启
`NEXUS_ALLOW_SOURCE_SDK_FIXTURE=ON`，其 manifest `publishable=false`，不能用于 promotion。
正式 clean 包准备、verify 和 consumer 全矩阵尚待最终交付证据；没有跨编译器／配置二进制 ABI 承诺。
完整 SDK 开发契约测试留在平台 checkout，安装包不接受开发 tests／contracts 开关。

## ARM 参考矩阵

| Board | 裸机 preset | FreeRTOS preset |
| --- | --- | --- |
| STM32F4 Discovery | `stm32-discovery-baremetal-release` | `stm32-discovery-freertos-release` |
| 启明欣欣 V3.1 | `stm32-qiming-baremetal-release` | `stm32-qiming-freertos-release` |
| 天空星青春版 | `stm32-sky-baremetal-release` | `stm32-sky-freertos-release` |
| 梁山派 | `gd32-liangshan-baremetal-release` | `gd32-liangshan-freertos-release` |

```sh
python3 scripts/build.py --preset stm32-qiming-baremetal-release
python3 external/nexus/scripts/ci/validate_firmware_elf.py \
  --build-dir build/stm32-qiming-baremetal-release/nexus \
  --report build/stm32-qiming-baremetal-release/firmware-static-contract.json
```

官方工具链准备入口是 SDK `scripts/ci/install_arm_toolchain.py`，校验锁定 archive SHA-256；
CMake 配置不下载依赖。ELF checker 读取真实向量、强中断、注册记录、加载权限与物理内存边界。
`--build-dir` 指向 SDK 子构建目录，报告明确 `hardware_verified=false`。
八应用具体支持组合见 [README](../README.md)；Native finite 和 ARM static 不能替代 HIL。
初始线上 CI 成功与本轮新 SDK 验收分别记录，不从 CI 定义推断执行结果。

## 当前能力限制

MCU typed I2C 尚未接入。启明／天空星未绑定 SPI；梁山派板载 SPI4 Flash 尚无本例 loopback
隔离工装，公开 SPI 示例只在 Native／Discovery 创建。Native 无 typed UART port。
微秒时序、通用 RS485 transport、网络、物理 watchdog、MCU crypto／熵源／可信计数器和
完整升级链没有因现有模型成为已验证能力。自定义产品 Kconfig schema 扩展也没有通用承诺。
产品发布、签名、制造授权和恢复政策由各产品仓库定义。
