# Stream recovery / 流恢复

Native executes real public model operations: immutable loans, capacity loss,
BUSY stop, release, restart. `h743/` independently selects supported continuous
DMA receive with explicit aligned storage and finite receive windows.
`h743_cache_on/` reuses the same C11 application with whole-line reservations
and an explicit Cache-on clock profile. Failed releases keep exact static loan
tokens; failed drain parks without dropping storage.
Native 执行公开模型的借用不变、容量丢失、BUSY stop、释放和重启；`h743/` 独立选择
当前支持的连续 DMA 接收、显式对齐存储和有限接收窗口。`h743_cache_on/` 共用
同一 C11 应用，选择完整 cache-line reservation 和显式 Cache-on 配置。release
失败保留精确静态 token；排空失败则保留存储并停留。

See [the bilingual guide](../../docs/developer-recipes.md) for exact behavior,
commands, slow-consumer assumptions and evidence boundaries. All physical HIL
remains `not_executed`; Native does not establish DMA timing or MCU qualification.
具体行为、命令、慢消费者假设及证据边界见[双语指南](../../docs/developer-recipes.md)。
全部实板 HIL 均为 `not_executed`；Native 不证明 DMA 时序或 MCU 实物资格。
