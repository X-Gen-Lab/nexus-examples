# 托管网关 F407 软件参考

本工程拥有一份 `assembly.toml`、`software-board/board.json` 和
`software_ca.h`，通过公开 Nexus 组件构建托管路由、安全网关和持久待发队列。
单个 build 选择一个精确 Nexus 源码版本。

Board 事实沿用启明 v3.1 来源审查中的 STM32F407ZGT6、HSE8／PLL168、RMII AF11
路线和内部 RNG。原始来源标识及待验证项保存在本地 `board.json`。
`physical_pcb_revision` 保持空值，实板测量状态为 `not_executed`。

`software_ca.h` 保留 Nexus `6b65869785` 中公开合成 CA 的 411 个 DER 字节。
SHA-256 为 `54947e5f22f375962c77a82ba0e3df7c4a26620396f41ac6c7c61218d4dcd905`。
产品提供自己的信任根、身份、可信 UTC 和熵声明。

冷态入口拒绝未资格化的熵、PHY 和 UTC 准入，保留关闭的硬件与栈状态。链接结果
核对实际 Flash LOAD 和预留 SRAM，资源报告已经包含 8 KiB MSP。

在示例仓库根目录、锁定 ARM GCC／CMake／Ninja 已准备的环境执行。

```sh
cmake -S apps/network_gateway/managed-f407 -B build/gateway-managed-f407 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/arm-gcc.cmake"
cmake --build build/gateway-managed-f407 --parallel 1
```

安装 SDK 时在同一配置命令中传入
`-DNEXUS_SOURCE=/absolute/prefix/share/nexus/src`，安装包需选择 `network`、`tls`、
`mqtt` 和 `flashdb` 组件。编译成功后检查
`bin/managed_gateway_f407.resources.json` 的精确镜像、配置、预算和实际结果。
