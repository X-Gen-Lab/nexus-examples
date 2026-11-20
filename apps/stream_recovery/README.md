# Stream recovery / 流恢复

本目录有可运行的 Native 流示例，以及受独立应用合同执行的有限接收策略。
`main.c`/`stream_application.c` 通过显式模型注入验证不可变借用、容量 LOSS、BUSY
stop、归还和 restart；`receive_application.c` 保留有限窗口、失败 release 重试
及关闭策略。两条路径使用真实公开接口，不声称已经运行 MCU DMA。

The Native application exercises immutable loans, capacity loss, BUSY stop,
release and restart. The finite receive fragment retains exact loan tokens on
failed release and keeps storage through failed drain. These are distinct
application contracts, not duplicate implementations.

诊断仅解释已复制事实，不推断服务频率，也不授权释放数据。

Diagnostics neither infer timing nor authorize reclamation. See the [application guide](../../docs/examples.md)
and [validation boundary](../../docs/validation.md). Physical HIL is `not_executed`.
