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

For explicit checkout development, replace `Nexus_DIR` with
`-DNEXUS_SOURCE=/absolute/path/to/nexus`. This does not establish SDK qualification.
需要源码联合开发时，可改用 `-DNEXUS_SOURCE=/absolute/path/to/nexus`；该路径不建立
SDK 发布资格。

The executable prints `Nexus starter: transfer settled; platform stopped` and
returns zero. Native UART is a deterministic software model, not a serial port.
程序输出 `Nexus starter: transfer settled; platform stopped` 并返回零。
Native UART 是软件模型，不是宿主串口，不建立任何 MCU 实板资格。

Change the baud or RX capacity in the authored TOML, then configure again. An
unknown mode or conflicting resource must fail; do not edit generated files.
修改手写 TOML 中的波特率或接收容量后重新配置。未知模式和冲突必须失败，勿编辑
生成文件。平台路径与编译器错误通过原始 CMake 诊断处理。
