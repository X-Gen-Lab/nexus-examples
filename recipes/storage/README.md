# 显式存储所有权

本入口复用 [journal](../../apps/journal/README.md)、
[工业控制](../../apps/industrial_control/README.md) 与
[Flash 维护](../../apps/flash_maintenance/README.md) 的真实应用路径。
应用选择区域、schema、工作区、维护时机与恢复策略；本目录不增加默认分区、
后台任务或转发式存储接口。

文件 journal 关闭并重开显式选定的 Native 文件。工业控制使用 Settings 保存应用
周期，在协议停止、请求结清和执行者 join 后提交。Flash 片段要求明确维护授权与
一个保留的可写 sector；GoogleTest 使用外部 Flash 模型。

从仓库根目录执行已有测试目标：

```sh
python scripts/build.py --preset native-debug --parallel 1
ctest --test-dir build/native-debug \
  -R '^(persistent_journal|industrial_control_reference|industrial_control_google|flash_maintenance_google|storage_reference_google)$' \
  --output-on-failure --no-tests=error
```

`journal` 与 `industrial_control` 的手工运行必须使用专用模型文件，不能指向用户数据。
测试中的 reopen／readback 只说明实际软件恢复或返回值，不证明物理掉电原子性。
Flash 失败可能已有部分擦写，TIMEOUT 不撤销借用，也不允许自动重放。

物理擦写、欠压、寿命与板级恢复均为 `not_executed`。SFUD 和实际 Flash 选型遵循
各自精确的消费者与依赖合同；本入口不选择未明确的器件。

## 数据库与快照消费者

[`database_reference.c`](database_reference.c) 是真实 C11 组合，公开合同见
[`database_reference.h`](database_reference.h)。调用者提供独立的 KVDB、TSDB、
已打开的 Settings、编码缓冲区和产品 schema。函数先显式打开数据库，在 Settings
中暂存周期，再立即保存独立 KV、编码带 ID/schema/CRC32 的记录并追加到 TSDB，
最后只有 `commit_snapshot=true` 才提交 Settings 完整快照。不同提交点不组成整体
原子事务；错误报告保留已经成功返回的观察，不表示介质回滚。周期单位为毫秒，
采用小端编码；TS 时间戳须由产品保证严格递增，事件去重 ID 须非零。

由单个产品执行者串行调用，显式设置本次操作作用域的 deadline getter，运行期间
排除其他所有 FlashDB 调用者。所有区域、工作区、时钟和临时存储均保留到函数返回；
每条退出路径都会清除 getter。一个有限的绝对 deadline 覆盖整次操作，调用前须为
Settings 存储适配器设置相同 deadline。FlashStorage 适配器通过
`nx_flash_storage_set_deadline()` 在串行操作之间设置；SFUD 还须在整次操作开始前
设置其显式读取 deadline。

`allow_create` 仅授权在全 FF 数据库区域初建。只打开已有数据的模式、损坏数据或
未知 schema 都不能授权自动格式化；schema 迁移由产品处理，文件系统 mount 失败
也不是格式化授权。发生修改错误时停止后续步骤，保留 owner，先完成物理恢复，
再关闭／重开判断真实存活数据；不要原样自动重放本函数。即使 Settings commit
随后失败，KV 或事件也可能已持久化。

Native `storage_reference_google` 使用外部 NOR 模型和真实公开 SDK 库，覆盖拒绝
初建、重开、立即 KV/TS 写入与 Settings 暂存／提交的区别、记录解码以及修改失败
后不格式化、不重放的行为。这不是可靠 outbox 投递实现；产品仍负责远端去重／ACK、
未确认记录保留和介质满时的背压。

未提交开发 SDK 的局部反馈不能代替所锁定 SDK 的正式集成验收。硬件掉电、寿命与
时序均为 `not_executed`。
