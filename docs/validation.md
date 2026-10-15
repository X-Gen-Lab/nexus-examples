> Historical previous-architecture record. This plan/result does not qualify
> the current static-port architecture. See [current validation](refactor-validation.md)
> and the pinned SDK `docs/design/next-generation-execution.csv` for current scope.

# 历史首批示例的软件验证记录

记录日期：2026-10-09（Asia/Shanghai）。本页冻结 Nexus `069dcf14` 的首批历史软件
集成证据，下面的 3 项 Native 测试、20 个 ARM ELF 和当时未执行项仅属于该基线。
本轮已测试 snapshot Nexus `db6ca0c4`／示例 `fcc17963`、后续交付 pin 与完整结果见
[最终软件验证](refactor-validation.md) 和
[platform-refactor-validation.json](../evidence/platform-refactor-validation.json)。
历史报告不替代当前软件验收，也不构成实板资格或产品发布报告。

## 源码与执行环境

- Nexus：`069dcf14f9cb1cd6fc4a869d87bf9827b91ce306`，tree
  `b135a2cf0294319a8f02b15c578b35433864a3aa`。
- 应用实现提交：`2a7b555`；构建入口和依赖核验提交：`7196841`。
- 本地显式使用 `/workspace/nexus` checkout；父仓库 Gitlink、SDK commit/tree、
  SDK 干净状态、7 个递归依赖 checkout 和 GD32 官方导入摘要均已核验。
- CMake 4.4.4、Native GCC 14.2.0、ARM GNU 14.3.Rel1
  （编译器报告 14.3.1 20250623）；Python 与工具二进制身份见 JSON 记录。
- 本次未重新下载或验证 ARM 安装包摘要；使用工作区已有工具链并记录实际编译器摘要。
  CI 定义使用 Nexus 的固定安装包 SHA-256 安装器，其线上执行状态另行记录。

机器可读记录见 [validation.json](../evidence/validation.json)。记录保存实际编译输入、
配置、可执行文件和 ELF 摘要、测试枚举、退出码及依赖身份。
构建发生在首次提交整理过程中，文档当时尚未全部提交；输入摘要与后续实现提交绑定，
不把这些结果标作最终 clean checkout 的候选发布验证。

## Native 实际运行

执行入口：

```sh
python3 scripts/build.py --preset native-debug --nexus-source /workspace/nexus --test --parallel 4
python3 scripts/test_repository.py --nexus-source /workspace/nexus --report build/dependency-identity.json
```

配置、编译、CTest 和依赖检查退出码均为 0。CTest 有 **3 项通过，0 失败、0 跳过**：

| 测试 | 实际观察范围 |
|---|---|
| `boot_blinky_finite` | 有限循环、GPIO 状态及正常退出 |
| `rtos_pipeline_finite` | Native producer/consumer、队列和有限时间运行 |
| `industrial_protocol_and_fault` | 软件 Modbus 帧、虚拟 plant 与过期传感器故障路径 |

当前 Native 没有类型化 UART provider，因此没有用替身生成 `uart_echo` 的通过结论。
两个构建脚本的 Python 编译检查通过。依赖检查还核验了 9 组 preset 的引用关系。
另实际执行了一次错误 SDK 身份检查：退出码为 1，旧报告已删除，未留下成功记录。

## ARM 实际编译、链接与 ELF 检查

每组执行本仓库 `scripts/build.py`，随后执行固定 SDK 的
`scripts/ci/validate_firmware_elf.py --build-dir build/<preset>/nexus`。
8 组构建和检查退出码均为 0，共检查 **20 个 ELF**。

| Board | 裸机 | FreeRTOS |
|---|---|---|
| STM32F4Discovery | 2 ELF，通过 | 3 ELF，通过 |
| 启明 STM32F407ZGT6 V3.1 | 2 ELF，通过 | 3 ELF，通过 |
| 天空星 STM32F407VET6 青春版 | 2 ELF，通过 | 3 ELF，通过 |
| 梁山派 GD32F470ZGT6 | 2 ELF，通过 | 3 ELF，通过 |

裸机镜像为 `boot_blinky`、`uart_echo`；FreeRTOS 另含 `rtos_pipeline`。
检查包括镜像入口、向量表、强中断 handler、设备注册条目、Flash/RAM 区域、
保留存储区和 ELF 段属性，具体逐镜像数据见 JSON。未执行 MCU 镜像。

MCU 使用 newlib-nosys，`rtos_pipeline` 的 `printf` 不是已接入的串口控制台；
统计先通过调试器观察。串口演示使用独立 `uart_echo` 的类型化 UART 接口。
链接器的 nosys 系统调用警告没有被解释为串口已可用。

## 证据位置与未执行项

本地 `build/<preset>/` 保留构建日志、有效配置、ELF 静态报告及产物；Native
保留 `ctest-results.xml`。提交中的 JSON 保存其摘要和检查数据，不包含大体积二进制。
CI 配置了对应报告与产物上传；配置存在不表示线上任务已运行通过。

未执行：全新网络 clone 的递归 submodule 获取、GitHub Actions、三块目标板刷写和运行、
物理 UART/LED/SPI 波形、DMA 停止及取消、Flash 物理掉电、工业 RS485、长期负载。
本次未改动 Nexus 核心，未重新运行其全量契约测试。
这些项目按 [平台计划](platform-refactor-plan.md) 和
[示例路线图](examples-roadmap.md) 验收，软件模型与 ARM 链接不能替代它们。
