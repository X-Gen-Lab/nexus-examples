# 授权寄存器示例 / Authorized register example

本例在 Native 组合八个寄存器、写入联锁、Modbus RTU、内存日志和公开 UART。
普通写入成功；未授权的受保护写入返回异常且保持原值。应用核对 UART TC、回复
字节及 CRC，拥有 request/event、缓冲、日志预算和模拟方向状态。

The finite host application owns register policy and transport storage. It checks
normal and denied writes, response CRC, wire completion and shutdown through real
application code. Modeled direction does not establish RS485 electrical behavior.

共享参考明确为 115200/8N1、每字符十位，不据此声明完整串行 Modbus 安装资格。
运行[完整 Native 构建](../../docs/build.md)；实际收发器、DE/RE、终端和静默间隔由
产品装配与实板验证决定，当前为 `not_executed`。
The selected model framing is explicit. A physical transceiver, wiring and serial
installation need independently reviewed composition and measurement.
