# 验收准备 / Acceptance preparation

当前范围以 [README](../README.md)、[Board](boards.md)、实际 preset 和源码锁为准。
历史结果保留其原 commit／配置／产物身份，不迁移成功状态。以下流程只描述必须
执行的验收，不提前声明通过。

The current lock, presets and Board packages define the scope. Historical
results retain their original identity and never qualify this changed pin.

## 软件集成 / Software integration

1. 完成平台提交 P，验证干净源码、官方依赖配对和 Source SDK。
   Freeze platform P and verify its clean source and selected dependencies.
2. 用候选模式先验证实际 Native、ARM 和 SDK 消费，修复应用 API 或配置问题。
   Exercise real candidate consumers before promoting the formal source pin.
3. 同时更新 Gitlink 和源码锁到 P，保留实际 tree；执行普通完整提交门禁得到 E。
   Update both formal identities together, then run the complete ordinary gate.
4. 执行本地 CI 的完整矩阵与 installed SDK；绑定 P、E、配置、工具与产物摘要。
   Bind actual commands and artifacts; a partial scope never qualifies the matrix.
5. 保持失败记录，拒绝空、跳过、陈旧报告与执行中的身份变化。
   Preserve failures and reject stale, empty, skipped or identity-drifting runs.

完整入口见[本地 CI](local-ci.md)。本例库的测试数量和资格独立于平台；平台门禁
不能替代应用测试。GitHub Actions 未执行时，记录 local，而不是托管 CI success。

The examples need their own actual execution. Platform test counts cannot
substitute for application integration. Local execution is labelled local.

## HIL readiness

四块 F407 Board 均未接入。准备工具生成 `prepared_unbound` 计划，所有物理案例为
`not_executed`。实际站点须观测 PCB revision、料号／UID、探针编号、稳定串口路径、
晶振、供电和接线，并绑定相同 ELF／resolved／预算与工具配置。

All four Boards remain disconnected. Observe real station identity and wiring
before binding a plan. Preparation never invents chip identity or measurements.

先完成启动、时钟、GPIO 安全电平、UART RX/TX/TC、IRQ 负载、停止和恢复闭环，
再按所选模式扩展 DMA、总线、模拟输出、显示／触摸。时间、吞吐、功耗和栈峰值
需要仪器及实际负载。Flash／掉电／看门狗使用明确授权的操作流程，默认不执行。

Qualify the smallest physical lifecycle first, then each selected mode. Retain
raw measurements, load, clock and instrument settings. Destructive maintenance
requires a product-owned reserved region and station procedure.

## 故障与保留责任 / Fault and retention duties

首故障通过 `nx_fault_observe` 记录。输出端口未实现或失败仍为 UNPROVEN，软件
返回值不证明安全电压。请求 ACCEPTED／QUARANTINED 后，描述符、buffer 和通知
目标持续保留；deadline、诊断或 scheduler 返回都不等于 SETTLED。

The first-fault record is diagnostic. Only actual hardware/IRQ withdrawal,
settlement and joins permit reclamation. If those facts cannot be established,
retain storage and let the product own its reset or maintenance decision.

历史准备和故障开发过程可从
[固定重构前文档](https://github.com/X-Gen-Lab/nexus-examples/blob/e48c442d78ddf438ed5be0b562e541dabc069b8f/docs/delivery-preparation.md)
追溯，原始 [fault-policy 记录](../tests/evidence/fault-policy/README.md) 保持不变。
Historical preparations and raw fault-policy evidence remain available at their
original source identities, without becoming current acceptance.
