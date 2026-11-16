# 文件 Flash 日志 / File-backed journal

本例仅在 Native 运行，应用选择专用模型文件、Flash 几何、两个区域、记录字节和
workspace，经 `Nexus::Storage` 与 `Nexus::FileFlash` 保存，再关闭重开检查恢复。
CTest 提供构建目录内的文件路径；手工运行时也应使用专用测试文件。

This finite host application saves and reloads application-owned data through a
file-backed Flash port. The application owns geometry, regions, workspace and
file path. It never reserves or erases MCU Flash.

关闭重开验证软件恢复，不证明物理掉电原子性。真实产品拥有分区、擦写维护窗口和
掉电策略。[主构建与验证](../../docs/build.md)执行该例，实板 HIL 为 `not_executed`。
Reopen behavior is a software check; physical brownout and erase scheduling need
separate product qualification.
