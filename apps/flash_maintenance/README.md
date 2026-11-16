# Flash 维护策略片段 / Flash maintenance policy fragment

本目录是受测试执行的 C11 应用策略，没有独立 `main` 或工程。调用者显式提供
可写保留 sector、授权、同一时钟域的绝对 deadline 和独立 32 字节 RAM payload；
编程单位必须整除 payload。策略执行一次 erase/program/readback，不自动重试。

This application fragment requires an explicitly authorized whole reserved sector
and caller-owned RAM. Tests execute its real C path with external Flash ports.
It is not a standalone firmware project and selects no physical partition.

拒绝授权不产生 Flash 操作；后续失败可能已经部分擦写，report 只记录成功返回的
步骤，不证明原子性。调用前须 join 其他 master，产品负责电压、看门狗、执行位置
和掉电恢复。此仓库不自动对设备执行破坏性操作。

Failure can follow partial physical progress. The caller owns serialized Thread
execution, other-master quiescence and maintenance policy. A stuck physical pulse
need not return; a deadline is not permission to reclaim a live buffer.

[验证指南](../../docs/validation.md)区分模型合同、ARM 资源和实板验收。
Physical maintenance and power-loss acceptance remain `not_executed`.
