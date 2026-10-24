# Nexus Examples

本仓库维护公开应用示例、装配配置和集成验证。Nexus 维护通用平台；客户协议、
私有产品、密钥、PCB 数据和部署策略由对应产品仓库维护。

当前代码消费静态 factory 与共享多态接口：`nexus/core`、`nexus/io`、`nexus/os` 和
`nexus/components`。`nx_platform_start()` 只建立已选择的基础设施，应用自己决定
任务、组件、资源预算和恢复策略。依赖通过 `external/nexus` Gitlink 和
[源码锁](dependencies/nexus.lock.json) 固定，最终验收必须与该身份一致。

当前集成面向 Nexus 0.2 开发周期，精确源码提交与 tree 以
[源码锁](dependencies/nexus.lock.json) 和 `external/nexus` Gitlink 为准。平台提供
官方 STM32F4 LL 复用和可选冷态生命周期诊断。SDK 元数据版本为 `0.2.0`，
不移动已发布的 `v0.1.0` 标签，
也不冒充该标签的原始产物。本仓库独立的应用工程版本仍为 `1.0.0`；精确依赖以
Gitlink 和源码锁为准。[0.1.0 集成说明](docs/release-0.1.0.md) 保留原始发布快照及
证据边界；该历史证据不自动证明当前迭代。MCU 实板 HIL 保持 `not_executed`。

## 应用与最小工程

| Target | Native 有限执行 | MCU 裸机 / FreeRTOS |
| --- | --- | --- |
| `platform_reference` | GPIO、显式 RX 注入、IRQ TX 完成和停止 | 八套软件参考配置的 GPIO 与 UART；FreeRTOS 静态任务 |
| `sensor_spi` | BMP280 SPI 端口、芯片身份与 Bosch 参考结果 | 暂无审定传感器板级接线 |
| `journal` | 文件 NOR 模型、关闭重开后恢复记录 | 暂无指定产品 Flash 分区与电源测试 |
| `modbus_registers` | 实际 UART 模型、CRC、写权限、异常回复、有限日志 | 暂无 RS485 收发器接线和方向时序验收 |
| `shared_spi_runtime` | 两个真实任务、两端点、满队列拒绝、取消、join/stop | 独立 F407ZG FreeRTOS 硅片软件 fixture；不声明实板双 CS 接线 |
| `stream_recovery` | 慢消费者、loss、BUSY stop、恢复及只读诊断 | 独立 H743 continuous DMA 软件参考，Cache off |
| `uart_owner_runtime` | 两个真实 producer 共享 owner，IRQ hint、cancel/drain/join、分阶段启动回收 | 八套参考配置、两后端，显式静态任务或裸机 pump |

Native 注册七个有限应用 CTest、六组 GoogleTest 执行，以及真实 FreeRTOS POSIX 内核
合同和共享 SPI 应用。新增模板、创建练习、三条生命周期路径及迁移步骤见
[双语工程指南](docs/developer-recipes.md)。四个版本化模板保存在 `templates/`。
UART 由应用显式注入接收事实，服务推进发送并等待真正
TC 完成。模型没有自动串口设备或后台 RX 生产者。Flash 文件示例验证宿主恢复流程，
不证明 MCU 掉电安全。三板为启明欣欣 F407ZG V3.1、天空星 F407VE 青春版和梁山派
GD32F470ZG。其余五套为 NUCLEO-H743ZI2、NUCLEO-U575ZI-Q、F103RB、
GD32F303RC 和 G474RE 软件参考；GD32F303RC 不指定实物 PCB 或 LED。当前实板
未接入，所有物理资格均未验证。

故障策略使用 Core `nx_fault_observe`。正式 SDK 身份以源码锁和 Gitlink 为准。
历史代码提交 `3de85238` 与固定 SDK `98270512` 已通过 8 项 Native CTest、三组共
43 项 GoogleTest、44 项 Python 工具测试，以及十个 ARM 配置的 20 个实际 ELF
预算检查。[验收记录](docs/soc-refinement-results.json) 固定完整提交、tree 和原始证据
哈希；后续文档提交不会自动获得新的代码资格。10 份 HIL 计划已准备，实板未执行。
旧 pin 的成功或失败只描述历史快照。
[交付准备](docs/delivery-preparation.md) 区分开发证据和最终软件资格。

## 开发与构建

```sh
git submodule update --init --recursive
python3 scripts/setup_dev_tools.py
.venv/bin/python -m pip install -r external/nexus/dependencies/environment-tools.txt
. .venv/bin/activate
python scripts/test_repository.py
python scripts/check_style.py --all
python -m unittest discover -s scripts -p 'test_*.py' -v
python scripts/build.py --preset native-debug --test
python scripts/build.py --preset stm32-qiming-baremetal
python scripts/build.py --preset stm32-sky-freertos
python scripts/build.py --preset gd32-liangshan-freertos
python scripts/build.py --preset stm32-h743-nucleo-baremetal
python scripts/build.py --preset stm32-u575-nucleo-freertos
```

ARM 构建还需 SDK 固定的 GNU Arm 工具链，可用
`external/nexus/scripts/ci/install_arm_toolchain.py --install-dir <目录>` 安装。
Windows 使用 `.venv/Scripts/python.exe` 并将工具路径加入 PATH。

每个配置使用自己的 `build/<preset>`，主示例配置输入是 `reproduction/assemblies/*.toml`；独立应用和模板各有唯一 `assembly.toml`。
配置器生成定型实例、只读接口、factory ID、链接脚本和预算。
`nx_factory_uart(NX_UART_ID_UART0)` 仅返回静态实例，不分配内存或启动硬件；
SPI 设备使用 `nx_factory_spi_device`，控制器与外部设备身份分开。
本仓库有 Native 加八个精确参考配置、两后端共十七个 preset；ARM 每组分别产生平台参考和共享 UARTOwner
两个实际链接的 ELF，并独立检查各自预算。

H743 初始范围是 HSI64、Cache off、PB0 LED 与 USART3 PD8/PD9 IRQ byte RX；
U575 是 MSIS4、TZEN0、PC7 LED 与 USART1 PA9/PA10 IRQ byte RX。两个软件参考均不
启用 DMA、Cache 或 TrustZone。`platform_reference` 优先读取公共 event 接口，只有
UNSUPPORTED 才转到公共 byte 接口；事件损坏或 byte overflow 会停止回显，不制造
字符、时间戳或丢失恢复结果。产品协议与恢复策略仍由产品仓库决定。

`platform_reference` 使用 `nx_platform_start_diagnosed()` 和
`nx_platform_stop_diagnosed()`，把可选阶段诊断写入调用者自己的存储。Native 失败时
打印实际 primary、cleanup、remaining effects 及阶段；MCU 不因此引入 logger、堆或
额外 worker。`controller_index` 是同一次装配 `resolved.json` 中 `controllers` 的零基
下标，**不是 factory ID**；`SIZE_MAX` 表示该阶段没有控制器。错误返回仍是行为依据，
诊断不会授权释放 buffer、复位或重试。真实生成平台的 GoogleTest 覆盖成功清空、
非法上下文拒绝、重复启动不接管原 owner，以及 UART 借用未结清时 stop 返回 BUSY、
继续 service 后才能释放。启动失败且 cleanup 未完成时，产品必须继续保留实际责任；
本示例不虚构自动恢复。

提交钩子检查暂存文件的格式、机械注释、文本和 Conventional Commit，并对行为变更
验证正式干净 SDK，执行全部工具测试、Native CTest 和 GoogleTest；不改写或暂存源文件。
执行前后须保持相同 index/SDK 身份，拒绝未暂存行为、过滤、跳过、缺失或陈旧报告。
C 格式和注释规则复用固定 SDK，避免复制一套规则。手动完整检查使用
`python scripts/tdd_gate.py --all`；CI 保留实际 Google discovery、XML、commands 和退出码。

变更行为先用失败测试定义合同，通过后再重构。Nexus 通用固件合同使用
GoogleTest／GoogleMock 宿主测试；示例固件保持 C11，目标映像没有测试框架。
Python 边界测试执行真实配置器，并检查十七组 TOML、生成 factory 和报告拒绝逻辑。

本地 `--nexus-source /absolute/path` 只改变 SDK 位置；正式依赖检查仍要求锁、
Gitlink、tree、干净源码和供应商身份一致。未提交的联合开发可以显式使用临时装配，
但其输出只能标为 `publishable=false`，不能冒充已封存发行验收。

[构建集成](docs/build-integration.md)、[板卡](docs/boards.md)、
[软件验证边界](docs/refactor-validation.md)、[后续迭代](docs/examples-roadmap.md)
说明当前接口和交付范围。旧架构的测试数量与证据保留为历史记录，不能证明新架构。

[多生产者 UARTOwner 示例](docs/os-owner-example.md) 说明唯一 execution owner、IRQ wake、
active cancel/drain 和 producer/owner join 后才能回收通知的顺序。GoogleTest／GoogleMock
通过固定 Nexus 源码检出中的 `cmake/testing/GoogleTest.cmake` 校验并使用固定
`ext/googletest` 子模块，配置时不联网，只进入 Native host 测试。已安装的固件
Source SDK 不包含该 helper 和宿主测试框架；MCU 示例保持 C11。

[UART FIFO 采集 / UART FIFO acquisition](apps/uart_fifo_acquisition/README.md)
提供 H743/U575 两个软件参考的独立有界流量应用；不新增 preset，也不建立实板资格。
Opt-in bounded UART traffic for the H743/U575 software references; the seventeen
presets stay unchanged and physical HIL remains unexecuted.
