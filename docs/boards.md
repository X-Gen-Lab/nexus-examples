# 当前参考板 / Maintained reference Boards

当前主工程只选择以下四个 STM32F407 Board，均使用已审核的 HSE 8 MHz／168 MHz
profile。Board ID 选择精确引脚和选装条件；原理图不能证明本次实物装配。

The four current Boards select the reviewed HSE8/168 MHz profile. Their exact
packages own routing and population choices; no connected PCB has been observed.

| Board ID | MCU | Status inactive level | Console |
| --- | --- | --- | --- |
| `stm32f407_qiming_v31` | STM32F407ZGT6 | PE3 high | USART1 PA9/PA10 |
| `stm32f407ve_sky_qingchun` | STM32F407VET6 | PB2 low | USART1 PA9/PA10 |
| `stm32f407zg_qiming_core` | STM32F407ZGT6 | PG13 high | USART1 PA9/PA10 |
| `stm32f407zg_qiming_v41` | STM32F407ZGT6 | PE3 high | USART1 PA9/PA10 |

每板有裸机与 FreeRTOS 两个 preset，只选择 GPIO 和 115200 baud UART IRQ/event。
RX 容量明确为 32 events；优先级为 5。FreeRTOS 配置显式声明 `calls_os=true`，
裸机不调用内核。UART 损坏、溢出或状态事件不被当作可回显字节。

Both backend presets select only GPIO and IRQ/event UART with a 32-event budget.
FreeRTOS explicitly permits the priority-5 notification to call the kernel.
The application rejects damaged reception before authorizing any echo.

UART binding 的候选模式由 `list-bindings --board <Board ID> --kind uart` 查询。
应用中的 `console` 是自定义角色，`uart1` 是板卡绑定；`mode = "irq"` 加
`rx.format = "events"` 对应规范模式 `irq-byte-event`。选择其他模式时从维护示例
开始，使用 `check` 检查整个装配，再查看 `explain` 的实际接口与资源。

Query UART choices with `list-bindings --board <Board ID> --kind uart`.
`console` is an application role and `uart1` a Board binding. The authored IRQ
event form resolves to `irq-byte-event`. Use a maintained recipe, then check
and explain the whole assembly; an editor's general schema cannot authorize a
new hardware mode or pin route.

核心板／V4.1 最小配置显式选择 SWD，保留 PA13／PA14；可选 NOR、SRAM、显示、触摸、
网络等状态均由 TOML 明确表达。最小工程选择 absent/disconnected 只是使用前提，
不是自动识别实物。ILI9341／XPT2046 型号不授权驱动共享接口或推断其他模块。

Core/V4.1 starters explicitly reserve SWD and declare optional module states.
An absent/disconnected selection is a prerequisite, not a population detector.
Known display and touch controller identities never enable other peripherals.

[`shared_spi_runtime`](../apps/shared_spi_runtime/) 的独立 Board 是 F407ZG 硅片
软件 fixture，声明 SPI1 PA5/PA6/PA7、PE4/PE3 CS。它不等同于任一开发板的已审
双设备接线。传感器、RS485、MCU Flash 分区等仍需应用自有的实际配置。

The shared-SPI Board is a silicon software fixture. Its explicit routes do not
establish physical sensor, chip-select or transceiver connections on these Boards.
Product-owned packages follow the external-Board procedure below.

HIL fixture 与 Board ID 一一选择，准备状态为 `prepared_unbound`。接入后应核对
PCB revision、完整料号、晶振、供电、探针、串口路径、逻辑电平及安全初始输出，
再执行启动、时钟、UART、IRQ/DMA、停止和故障恢复。Flash／掉电测试使用产品
明确授权的保留区；示例不在启动时自动擦写。

Physical HIL remains **`not_executed`**. Prepared work plans do not establish
observed UID, wiring, electrical timing, watchdog reset or power-loss behavior.
Retired Board examples remain at the fixed source in [history](history.md) and
never produce current station plans.

## 自有 Board 包 / Product-owned Board packages

平台拥有 CPU/SoC/provider；产品拥有 PCB 事实、TOML、应用存储和恢复策略。
`apps/shared_spi_runtime/software-board/board.json` 是精确 F407ZG 硅片软件例子，
没有声称实际 PCB 或器件。公共 Native 测试板位于 `tests/support/native-board`，
只在显式宿主模型装配中使用，不成为某个工业应用的私有依赖。

The product owns routing and assembly while the platform owns hardware behavior.
The shared-SPI package is a software fixture. The common Native Board belongs to
explicit host-test support, not to an application-private source directory.

以下练习只复制小型 Board 描述，不复制 HAL/provider。真实产品应在独立仓库维护
经过原理图审核的板级事实；相对 Board 路径随 TOML/产品目录整体移动。

Copy only the declarative Board fixture for this configuration exercise. A real
product needs its own reviewed facts; a Board string cannot add a missing SoC,
unsupported controller mode or physical wiring.

```sh
NEXUS_SOURCE="$PWD/external/nexus"
PRODUCT_DIR="$PWD/build/product-exercise"
mkdir -p "$PRODUCT_DIR/boards/software-fixture"
cp apps/shared_spi_runtime/software-board/board.json \
  "$PRODUCT_DIR/boards/software-fixture/board.json"
python "$NEXUS_SOURCE/tools/configure/configure.py" init \
  --board-package "$PRODUCT_DIR/boards/software-fixture" \
  --backend baremetal --clock hse8-pll168 \
  --profile gpio --binding primary-cs --instance select \
  --output "$PRODUCT_DIR/platform.toml"
python "$NEXUS_SOURCE/tools/configure/configure.py" provider-check \
  --assembly "$PRODUCT_DIR/platform.toml" \
  --expect-family stm32f4 --expect-part STM32F407ZGT6 --json
python "$NEXUS_SOURCE/tools/configure/configure.py" explain \
  --assembly "$PRODUCT_DIR/platform.toml" --json
```

`provider-check` 验证 typed resolution、静态构造及输入身份，不等于编译或实板。
修改只发生在 TOML/Board 源；不要编辑生成的 resolved，也不要在冲突后静默选择
另一条路线。可用现有 `diff --assembly <before> --against <after> --json` 审核
变化，并用 `check --assembly <after> --diagnostics-json` 获取具体拒绝上下文。

These tools check configuration and explain resource ownership. Correct authored
inputs when rejected; generated output and diagnostics never authorize releasing
borrowed requests or replacing a failed hardware route.

完整双端点软件固件使用原应用与本地 TOML，从独立目录构建：

Build the maintained two-endpoint application rather than treating a GPIO
configuration exercise as a complete firmware application:

```sh
cmake -S apps/shared_spi_runtime -B build/board-source -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NEXUS_SOURCE/cmake/toolchains/arm-gcc.cmake" \
  -DNEXUS_SOURCE="$NEXUS_SOURCE" -DCMAKE_BUILD_TYPE=Release
cmake --build build/board-source --target shared_spi_runtime --parallel 2
```

安装 SDK 路径改传 `Nexus_DIR`、同一 commit 的
`NEXUS_EXPECTED_SOURCE_REVISION` 和 prefix 内的 ARM toolchain，使用另一个
构建目录。两者消费同一应用和装配，不复制硬件实现。producer、owner 排空及
`join(NEVER)` 仍不保证有限停机；在实际排空和 join 前必须保留静态借用存储。

The installed form selects `Nexus_DIR`, its expected revision and its toolchain
in a separate build directory. Both forms share application and assembly.
Static storage remains live through failed drain or join; linking is not a
measurement of wires, timing or bounded application shutdown.
