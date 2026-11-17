# qiming starter / 最小裸机工程

The exact Board is `stm32f407_qiming_v31`, clock profile `hse8-pll168`.
本工程选择精确 Board `stm32f407_qiming_v31` 和时钟配置 `hse8-pll168`。

The product owns one GPIO and UART, static request storage and explicit service.
The greeting completes before platform stop. A timeout does not revoke storage;
a stuck drain requires the product's reviewed recovery policy.
产品拥有 GPIO、UART、静态请求和执行循环。问候消息真正结清后才停止平台；超时
不会释放仍被借用的数据，排空失败由产品恢复策略处理。

Use the pinned ARM toolchain and a verified Source SDK:
使用锁定 ARM 工具链及已验证源码 SDK：

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/sdk/share/nexus/src/cmake/toolchains/arm-gcc.cmake \
  -DNexus_DIR=/path/to/sdk/lib/cmake/Nexus
cmake --build build --parallel 2
```

Explicit checkout development accepts `-DNEXUS_SOURCE=/absolute/path/to/nexus`
instead of `Nexus_DIR`, with that checkout's toolchain file.
联合开发可改用 `-DNEXUS_SOURCE=/absolute/path/to/nexus` 及该检出的工具链文件；
这不替代正式 SDK 身份验证。

Read the selected Board README before connecting equipment. The UART and GPIO
use only maintained bindings. PCB revision, jumper, electrical continuity and
actual operation remain unverified until equipment executes them. Never infer
new peripheral wiring from this compilable example.
连接设备前阅读对应 Board README。本例仅使用已维护绑定；PCB 修订、跳线、实际
连通性和运行结果仍需实板验证，不能从编译成功推断其他外设接线。

Expected UART output is `Nexus starter ready` at 115200 baud. Startup toggles the
selected GPIO once; platform stop restores its maintained stop state.
预期串口以 115200 波特率发送 `Nexus starter ready`。启动后仅翻转一次所选 GPIO，
停止平台时按既有合同恢复状态；这不是持续闪灯程序。物理 HIL：`not_executed`。

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
