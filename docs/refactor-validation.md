# 当前平台统一交付验证

当前结论仅绑定 Nexus `a4d5ac6989c741242f6ecd3810e0f9acbdb3bbe2`（tree `1a5d811afe5b6978d6902c56272763110437905f`）与 examples `85c1e2678e238d30a383a98d975bcf68d1055c3a`（tree `0e55a577751cdd01bfbd0a66dc7a91131c64294c`）。examples Gitlink/SDK lock 均选择该 Nexus 版本。随后证据提交与被测实现提交分开，最终证据提交身份由外部交付清单记录。历史结果保留各自源版本，不能累加或继承。

| 实际执行范围 | 结果 |
|---|---|
| Native Release 完整 CTest | 1897/1897，零失败/错误/跳过，枚举与实际 JUnit 一致 |
| Native sanitizer 完整 CTest | 1897/1897，零失败/错误/跳过，枚举与实际 JUnit 一致 |
| 平台 ARM 软件矩阵 | 8 配置、8 contract ELF 实际链接与静态检查 |
| 外部示例 | Native 7 个实际检查；ARM 8 配置、38 ELF；76 个 BIN/HEX 比较 |
| 严格可重定位源码 SDK | 3 Native C/C++/parent 程序实际运行、2 ARM ELF 检查、6 个身份/篡改拒绝 |
| 当前双仓 Python suites | Nexus 396 + examples helper 9 = 405 个实际枚举用例，零失败/跳过 |
| HIL 只读准备 | 3 板固件准入与 3 空站点真实拒绝；设备操作为空 |
| 公共 API 文档 | 真实 Doxygen XML、Sphinx EN/zh_CN -W 重建，零抑制/零诊断 |
| GitHub Actions | 实际 checkout tree、完成 job 与结果见权威记录 |

Native sanitizer 实际生成的 74/74 个二进制均链接 ASan/UBSan 运行时；使用默认泄漏检测，无抑制或环境覆盖，实际执行无 sanitizer 诊断。

完整命令、exit、枚举、JUnit、源码/依赖/配置/工具链、Board/layout、ELF/BIN/HEX 与日志身份见 [当前机器可读记录](../evidence/platform-refactor-validation.json) 和其 immutable reports 引用。

这些结果完成软件与 HIL 工装范围。实板尚未连接；探针、刷写/读回、串口、电气/IRQ/DMA、掉电和长期负载没有执行。Native UART/Flash 是有界主机模型，不证明物理串口、MCU Flash 或掉电恢复。MCU typed I2C、UART DMA、非零 image offset/bootloader、MPU/cache、MCU trust/security 链及企业/LTS/发布资格仍按平台支持矩阵明确约束。原 38 RF 项和 19 批次计划保持完整，软件通过不替代其全部实板/人员/发布退出条件。
