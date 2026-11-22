# F4 裸机 STOP 与重新接入

`baremetal_stop_reference.c` 消费所选平台的真实公开 API。它只适用于已审核的 F4
baremetal STOP profile，不恢复调度器 tick，也不提供 FreeRTOS STOP 组合。

调用前由应用关闭新工作、退出网络／UI 等业务、join 所有请求及观察者，并对实际
`nx_clock_domain_t` 调用 `nx_clock_domain_close` 取得 barrier。所有时钟与 deadline
借用者都必须登记在该域内，额外未登记消费者仍是应用责任。

```text
应用停止准入并 join → close clock admission
  → Platform suspend：保留 System、归还控制器
  → power_enter：检查 barrier、DMA、IRQ 与精确唤醒条件
  → 恢复 System，返回新的 Core time epoch 收条
  → reopen clock admission
  → Platform resume：重建同一批选定 owner
  → 应用检查完整 report 后创建新业务请求
```

`example_stop_report_t` 分别保存 suspend、power、reopen 和完整 resume 结果。
suspend 失败时不进入 STOP；reopen 失败时不启动控制器。power 拒绝返回后仍尝试
恢复原 owner，原始 power 错误保留为返回值，恢复错误单独保留在 report。
无法恢复 System 的底层路径按平台合同 park，不会返回一个可继续运行的假成功。

唤醒后旧 deadline／请求／网络连接不再自动生效。需要跨域时间预算时，调用者为
成功 receipt 提供独立审核的 RTC elapsed 上下界，再显式使用
`nx_stm32_deadline_bridge`；结果是范围，不是允许重放旧请求。重连和 UTC uncertainty
由应用新会话处理。单 bank Flash 写擦和未退出 DMA 不因选择低优先级而变为安全。

本参考不选择 GPIO、RTC 唤醒源、晶振或 PCB 接线，options 来自精确 Board／System
审核。Standby 具有复位语义，直接拒绝；它需要单独的 RTC owner 和启动流程。

`power_reference_google` 用 GoogleMock 置换外部 Platform／System 边界，验证的是
真实应用 C 的调用顺序、失败和收条保留。实际时钟恢复由平台自己的生产 C 测试
覆盖；ARM 链接、RTC 保持、物理功耗与唤醒时序各有独立证据，HIL 未执行。
