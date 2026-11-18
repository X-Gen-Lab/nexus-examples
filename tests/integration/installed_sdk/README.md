# Installed SDK integration / 安装版 SDK 集成

本入口用 `find_package(Nexus)` 发现精确正式 Source SDK，再通过
`nexus_add_platform(ASSEMBLY ...)` 显式装配，复用同一例库 `apps/`
中的 C11 应用和 `configs/` 中的装配，不复制平台实现。它属于完整例库，不能只复制
本子目录使用；独立新工程应从 `templates/` 创建。

This project belongs to the complete examples checkout. It reuses application
sources and explicitly creates its authored assembly after package discovery,
with no source-tree SDK fallback. Standalone
copied projects are provided by the versioned starters.

当前默认 Native 执行范围为平台参考与工业应用两个 CTest；网关目标的编译不代表
所有网关场景执行。正式 CI 在消费前后校验包完整性、预期 commit/tree 和 fresh
JUnit。ARM 使用同一 SDK prefix 的工具链、精确装配与独立构建目录。

The default Native run executes two application CTests. Package hashes and exact
revision are verified independently of compilation. See the
[build guide](../../../docs/build.md) and
[validation contract](../../../docs/validation.md) for maintained commands.
