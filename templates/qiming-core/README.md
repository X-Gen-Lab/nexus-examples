# qiming-core / 启明欣欣 F407ZGT6 核心板最小工程

Exact Board: `stm32f407zg_qiming_core` (`boards/stm32f407zg_qiming_core/board.json` in Nexus),
STM32F407ZGT6, `hse8-pll168`, baremetal. This template preserves the existing
V3.1 starter separately; it does not replace that PCB definition.
精确板卡：启明欣欣 F407ZGT6 核心板，选择上述独立 Board 路径、STM32F407ZGT6、
`hse8-pll168` 和裸机后端；原 V3.1 模板及板级定义继续保留。

The existing public application is reused unchanged. Logical `status` binds
`led0` on PG13; `console` binds `uart1` on USART1 PA9/PA10 at 115200 baud.
It starts the platform, toggles GPIO once, sends `Nexus starter ready`, services
its static TX request until SETTLED, checks the result and stops the platform.
复用已有公共接口应用：`status` 对应 PG13 的 `led0`，`console` 对应 USART1
PA9/PA10 的 `uart1`。启动、翻转一次 GPIO、发送问候并显式 service，确认静态 TX
请求真正 SETTLED 后才停止。超时不释放借用；无法排空时由产品提供恢复策略。

`assembly.toml` explicitly selects SWD and every Board hardware choice. Optional
modules are declared absent and shared connectors disconnected for this minimal
profile. These are required setup conditions, not auto-detection or evidence of
DNP status. Check the actual board, jumpers and disconnected modules against the
Board README before use. A fitted ILI9341/XPT2046 does not become selected by its
name: this profile contains no display, touch, media, network or calibration
behavior. Use a separately reviewed composition to enable fitted modules.
TOML 明确选择 SWD，并逐项声明所有 Board hardware 选项。本最小配置要求可选模块
未连接、共享接口断开；这些是使用前提，不是自动检测、DNP 判定或实际装配证明。
上电前核对板卡说明、跳线和接线。已确认型号为 ILI9341/XPT2046 的模块也不会自动
启用；本工程没有屏幕、触摸、媒体、网络或校准策略，启用时应另建审定配置。

Use a verified Source SDK containing the exact Board above:
使用包含上述精确 Board 的已验证源码 SDK：

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/share/nexus/src/cmake/toolchains/arm-gcc.cmake \
  -DNexus_DIR=/path/to/sdk/lib/cmake/Nexus
cmake --build build --parallel 2
```

Explicit source development instead uses `-DNexus_DIR=/absolute/nexus/cmake` and
that checkout's `cmake/toolchains/arm-gcc.cmake`. This changes the source location;
it does not bypass the examples source lock or establish release qualification.
联合开发可改用 `-DNexus_DIR=/absolute/nexus/cmake` 与该检出的工具链；这不替代示例
仓库的源码锁、干净检出或正式验收。源码配置与安装 SDK 消费需在更新锁后分别验证。

Physical HIL: `not_executed`. Configuration, ARM linking and installed SDK
consumption are separate evidence levels. Verify both consumption paths for
the SDK identity selected by the product lock; configuration alone is insufficient.
实板 HIL 为 `not_executed`。配置检查、ARM 链接和安装 SDK 消费是不同证据；应按
产品选定的 SDK 身份分别验证两条消费路径，不能只凭配置可解析声明交付资格。

## 编写配置 / Authoring the assembly

The copied `.taplo.toml` associates only `assembly.toml` with the exact SDK
commit's schema in a Taplo-compatible editor. The first remote schema fetch
needs network access; it is optional and never used by the build. For offline
editing, set its `schema.path` to the selected local SDK's
`tools/configure/assembly.schema.json`. Update the association when changing SDKs.
随工程复制的 `.taplo.toml` 为支持 Taplo 的编辑器关联精确 SDK 提交的 schema，
仅匹配 `assembly.toml`。首次远端读取需要联网；它是可选编辑辅助，不参与构建。
离线编辑时将其中 `schema.path` 改为本地所选 SDK 的
`tools/configure/assembly.schema.json`，切换 SDK 时同步更新。

Set `NEXUS_SOURCE` to a checkout or an installed SDK's `share/nexus/src`, then run:
将 `NEXUS_SOURCE` 指向源码检出或已安装 SDK 的 `share/nexus/src`，再运行：

```sh
python "$NEXUS_SOURCE/tools/configure/configure.py" check --assembly assembly.toml
python "$NEXUS_SOURCE/tools/configure/configure.py" explain --assembly assembly.toml
```

`status` and `console` are application roles; bindings come from the selected
Board. Change the TOML, inspect the effective interfaces/resources with `explain`,
then build. `rx.capacity = 32` means 32 events, not 32 bytes; the current ARM
layout uses 16 bytes per event (512 bytes for this selected array).
`status`、`console` 是应用角色名，binding 来自所选 Board。修改 TOML 后先检查
`explain` 给出的实际接口和资源，再构建。`rx.capacity = 32` 表示 32 个事件，
不是 32 字节；当前 ARM 布局每事件 16 字节，该数组占 512 字节。
