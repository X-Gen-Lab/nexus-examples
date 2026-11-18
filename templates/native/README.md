# Native starter / Native 最小工程

This product owns `main.c`, `assembly.toml` and its CMake target. The strong
factory references select one GPIO and UART. Startup creates no task; the sole
application owner services the accepted transfer until actual settlement, then
stops the platform. The static request and bytes remain alive during timeout or
failed drain. A production recovery policy is deliberately a product decision.

本工程拥有 `main.c`、`assembly.toml` 和 CMake 目标。强类型 factory 引用选择一个
GPIO 和 UART。启动不创建任务；应用是唯一执行者，持续推进已接受的发送，真正
结清后再停止平台。超时或排空失败时，静态请求和数据仍然有效；实际产品需提供经过
审查的恢复策略。

With prepared host tools and a verified Source SDK, build from this directory:
工具已准备且已有验证的源码 SDK 时，在本目录构建：

```sh
cmake -S . -B build -G Ninja -DNexus_DIR=/path/to/sdk/lib/cmake/Nexus
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error
```

For checkout development, set
`-DNexus_DIR=/absolute/path/to/nexus/cmake`. This does not establish SDK qualification.
需要源码联合开发时，可改用 `-DNexus_DIR=/absolute/path/to/nexus/cmake`；该路径不建立
SDK 发布资格。

The executable prints `Nexus starter: transfer settled; platform stopped` and
returns zero. Native UART is a deterministic software model, not a serial port.
程序输出 `Nexus starter: transfer settled; platform stopped` 并返回零。
Native UART 是软件模型，不是宿主串口，不建立任何 MCU 实板资格。

The Native-only CMake project explicitly enables `NEXUS_TEST_SUPPORT`.
Production F4 starters keep host test support disabled.
此 Native 工程明确启用 `NEXUS_TEST_SUPPORT`；F4 生产工程保持宿主模型支持关闭。

Change the baud or RX capacity in the authored TOML, then configure again. An
unknown mode or conflicting resource must fail; do not edit generated files.
修改手写 TOML 中的波特率或接收容量后重新配置。未知模式和冲突必须失败，勿编辑
生成文件。平台路径与编译器错误通过原始 CMake 诊断处理。

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
python "$NEXUS_SOURCE/tools/configure/configure.py" check --assembly assembly.toml --test-support
python "$NEXUS_SOURCE/tools/configure/configure.py" explain --assembly assembly.toml --test-support
```

`status` and `console` are application roles; bindings come from the selected
Board. Change the TOML, inspect the effective interfaces/resources with `explain`,
then build. `rx.capacity = 32` means 32 events, not 32 bytes; the current ARM
layout uses 16 bytes per event (512 bytes for this selected array).
`status`、`console` 是应用角色名，binding 来自所选 Board。修改 TOML 后先检查
`explain` 给出的实际接口和资源，再构建。`rx.capacity = 32` 表示 32 个事件，
不是 32 字节；当前 ARM 布局每事件 16 字节，该数组占 512 字节。
