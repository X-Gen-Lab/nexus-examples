# 固定历史资料 / Immutable history

当前源码树只维护运行所需示例、合同测试和使用说明。已退出的计划、旧交付报告、
RED/GREEN 日志、XML、源码副本、对象文件和归档原件保存在固定 Git 提交：

Historical plans, reports and original execution artifacts remain recoverable at
one immutable committed source. They are not current implementation or acceptance.

- [历史提交 / Historical source `98156b6b68a067628be725cf34d908463ac2b745`](https://github.com/X-Gen-Lab/nexus-examples/tree/98156b6b68a067628be725cf34d908463ac2b745)
- [原始历史 evidence](https://github.com/X-Gen-Lab/nexus-examples/tree/98156b6b68a067628be725cf34d908463ac2b745/evidence)
- [原始测试开发记录 / Test-development records](https://github.com/X-Gen-Lab/nexus-examples/tree/98156b6b68a067628be725cf34d908463ac2b745/tests/evidence)
- [旧文档与结果 / Previous documentation and result indexes](https://github.com/X-Gen-Lab/nexus-examples/tree/98156b6b68a067628be725cf34d908463ac2b745/docs)
- [更早的多 SoC 工程 / Retired multi-SoC source](https://github.com/X-Gen-Lab/nexus-examples/tree/e48c442d78ddf438ed5be0b562e541dabc069b8f)

历史原件保留自身 commit、配置、工具和执行身份；失败不改成成功，旧成功不提升为
新 pin 的资格。当前树退出这些文件不删除 Git 历史，也不把历史内的旧命令恢复为
当前支持入口。通过 Git 查看具体文件时，始终使用上方完整提交。

Original failures and passes retain their own source scope. Removing duplicate
records from the active tree does not rewrite history or restore retired
configuration paths. Use the full fixed revision when recovering any member.

当前使用入口见[构建](build.md)、[示例](examples.md)、[板卡](boards.md)和
[验证](validation.md)。维护中新执行保存在忽略的 `build/` 输出，按同一源码身份
保留一份可恢复原件。

Current commands live in the linked guides. New evidence stays in ignored output
and one recoverable artifact; it is not repeated in the source tree.
