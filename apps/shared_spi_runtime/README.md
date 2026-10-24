# Shared SPI runtime / 共享 SPI

Two producers, one executor, two endpoint identities and explicit static storage.
两个生产任务、唯一执行任务、两个设备身份，静态存储由应用拥有。

Read [the bilingual lifecycle, build and validation guide](../../docs/developer-recipes.md).
完整生命周期、命令与验证边界见[双语指南](../../docs/developer-recipes.md)。

The standalone FreeRTOS assembly is an F407ZG silicon software fixture, not a
physical Board wiring claim. Native and real FreeRTOS POSIX executions use the
root build. HIL: `not_executed`.
独立 FreeRTOS 装配是 F407ZG 硅片软件 fixture，不代表任何实板接线。Native 与真实
FreeRTOS POSIX 执行使用仓库根构建。实板 HIL：`not_executed`。
