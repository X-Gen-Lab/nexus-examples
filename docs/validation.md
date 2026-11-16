# 验证与交付 / Validation and delivery

当前验证绑定例库源码、`dependencies/nexus.lock.json`、`external/nexus` Gitlink、
装配、工具链和实际产物。写有 target、旧报告或平台主库的通过数量都不能替代本库
执行。此页定义规则，不提前声明某个新 pin 已通过。

Validation binds actual examples source, exact SDK, configuration, tools and
artifacts. Authored targets, historical reports and platform test counts do not
qualify changed application code. This page defines admission, not a future pass.

## 软件验证 / Software checks

| 检查 / Check | 必须观察的事实 / Required observation |
| --- | --- |
| 源码身份 / Source identity | 锁、Gitlink、干净 SDK、选中递归依赖一致 / Matching exact identities |
| 格式与注释 / Style | SDK 锁定工具、现有格式和 Doxygen 合同 / Locked tools and existing policy |
| Native 合同 / Native contracts | 全部维护 CTest、真实应用 C、Google 完整发现与执行 / Complete unfiltered execution |
| 工具回归 / Tooling | 实际配置、拒绝、撤回及 fresh-report 行为 / Real success and failure paths |
| ARM 镜像 / ARM images | 四板两后端的两个主 ELF，逐个检查配置、向量和资源 / Independent ELF checks |
| 独立工程 / Standalone projects | 已维护共享 SPI 与四 MCU starter / Existing standalone consumers |
| Installed SDK | 精确正式包、同源应用消费、fresh JUnit、消费后完整校验 / Same-source consumption and integrity |

普通 commit hook 执行完整 Native/Google 和工具门禁，拒绝不同的未暂存行为、
变化的 index、过滤、空报告、全部跳过、陈旧 XML 和身份漂移。保留 pre-commit、
Conventional Commit 及实际返回码；hook 验证执行，不自动证明历史 RED 顺序。

The ordinary hook validates the complete index and actual execution. It never
stages fixes or accepts filters, empty/skipped/stale output or source drift.
TDD evidence retains the original expected RED, smallest implementation and GREEN;
pure file moves do not invent earlier RED executions.

从[构建指南](build.md)运行完整本地 CI。部分 scope/preset 明确为部分结果；GitHub
Actions 未执行时记录 local，不写成托管 CI success。Source SDK 按选定 profile
导出，不携带未选库存或宿主 GoogleTest；完整固件配置须重新编译，无跨配置二进制
ABI 承诺。

Complete local CI reuses maintained presets and checkers. Partial runs remain
partial; local execution is not hosted CI. SDK profiles select dependencies, and
source-based ABI requires rebuilding the selected configuration.

## 输出与历史 / Output and history

新执行选择 `build/` 下独立输出目录。保存实际命令、退出码、日志、fresh JUnit、
source/SDK/配置摘要、ELF/BIN/MAP 和预算报告。缓存可复用，成功报告不能缓存为新
资格；先前失败保留原始范围和 bytes。保留唯一原件或可恢复归档，不将多轮日志、
源码副本或 `.o` 再提交到当前源码树。

Use a new ignored output directory for each run. Retain raw failures and source
identity with reports and artifacts. Cache tools/build inputs, never successful
test reports. Archive one recoverable evidence copy rather than committing
repeated logs and before-images. Fixed historical material is indexed in
[history](history.md) and does not qualify this checkout.

完整交付需先冻结平台提交 P，再同步例库 gitlink/lock、执行例库普通门禁得到 E，
最后关联 P、E、模板清单与实际软件证据。版本号、声明能力和软件验证均不等同于
产品发布、安全认证或 LTS 承诺。

Freeze the platform first, integrate its exact pin, then bind the committed
examples source and executed consumer evidence. Version declarations and software
checks establish neither product release nor safety certification.

## 实板准备 / Physical preparation

四块 F407 Board 尚未接入。HIL 工具只生成 `prepared_unbound` 计划；物理状态为
**`not_executed`**。接入后真实观测 PCB revision、完整料号/UID、探针、稳定串口
路径、晶振、供电、接线、模块与安全初始电平，再绑定同一 ELF、resolved 和工具。

No connected station has been observed. Unbound plans do not invent UID, probe,
wiring or measurements. Bind physical results only after observing the actual
station and matching software artifacts.

先验证启动、时钟、GPIO、UART RX/TX/TC、IRQ 负载、停止和恢复，再扩展所选 DMA、
传感器、RS485、显示触摸、模拟及联网模式。延迟、吞吐、功耗、栈峰值和振荡器精度
需要实际设备与负载。Flash/掉电/看门狗须产品自有保留区和明确工装流程，默认不做
破坏性操作。

Qualify the smallest board lifecycle first, then exact selected modes. Physical
latency, throughput, power, stack peaks and reset/power-loss behavior require
measurement. Application maintenance admission never follows from a diagnostic
or an accepted software callback alone. See [Board facts](boards.md).
