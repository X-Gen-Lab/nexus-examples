# Shared SPI runtime / 共享 SPI

Two producers, one executor, two endpoint identities and explicit static storage.
两个生产任务、唯一执行任务、两个设备身份，静态存储由应用拥有。

Read [the bilingual lifecycle, build and validation guide](../../docs/examples.md).
完整生命周期、命令与验证边界见[双语指南](../../docs/examples.md)。

The standalone FreeRTOS assembly is an F407ZG silicon software fixture, not a
physical Board wiring claim. Native and real FreeRTOS POSIX executions use the
root build. HIL: `not_executed`.
独立 FreeRTOS 装配是 F407ZG 硅片软件 fixture，不代表任何实板接线。Native 与真实
FreeRTOS POSIX 执行使用仓库根构建。实板 HIL：`not_executed`。

## 单执行者有限 DMA / Single-executor finite DMA

`finite_dma.c/.h` 是独立于多 producer owner 的小应用状态。`finite_dma_main.c`
使用同一物理 controller 的 strong endpoint，借用独立 TX/RX span；`step` 最多
推进一次 service，只有 acquire 观察 SETTLED 才结束借用。DMA TC、SPI 线上完成、
CS 撤回分别由真实 provider 证明。隔离时保留全部静态对象，不宣称已清理 Platform。

This path uses one executor without a queue, worker or extra controller owner.
Admission checks the exact endpoint/controller identity and complete payload
separation from the application control object. Cancel and timeout retain both
spans. Idle recovery is attempted only after settlement and never replays the
command or overwrites its original result. BUSY finish retains the port.

`dma.toml` 是明确的 STM32F407ZG 硅片软件参考，选择普通有限 DMA endpoint；
它不宣称板卡接线或外接 NOR 型号。四字节命令只是有限全双工传输数据，返回内容
不用于判断实物芯片身份。多段 DMA transaction 使用另一类公共接口，不在此混用。

From the repository root after preparing the toolchain and pinned SDK:

```sh
python scripts/verify_sdk.py
cmake -S apps/shared_spi_runtime -B build/finite-spi-dma -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-gcc.cmake" \
  -DNexus_DIR="$PWD/external/nexus/cmake" \
  -DNEXUS_ASSEMBLY_FILE="$PWD/apps/shared_spi_runtime/dma.toml" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/finite-spi-dma --target finite_spi_dma --parallel 2
```

`finite_spi_google` 以真实应用 C 验证完整 TX/RX 借用、拒绝、单步推进、隔离／取消、
失败停止重试及控制跨度别名。模型、ARM 链接和物理时序是独立范围；HIL 为
`not_executed`。
