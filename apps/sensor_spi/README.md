# BMP280 SPI 参考 / BMP280 SPI reference

本例仅在根 Native 工程运行。应用显式填充 Bosch 参考标定与原始寄存器，经公开
SPI endpoint 和 `Nexus::BMP280SPI` 计算并核对 25.08 °C、100653 Pa。
时钟域、设备、缓冲和轮询时机由应用拥有；没有隐式 worker、设备发现或真实传感器。

This finite Native application injects Bosch reference registers and checks the
calculated sample through the real public SPI transport. The model does not
establish physical sensor wiring, voltage or bus timing.

使用[主工程构建入口](../../docs/build.md)运行完整 Native 验证。
The root build owns its target and authored assembly. Physical HIL is `not_executed`.
