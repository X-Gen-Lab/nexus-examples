# Explicit short SPI transaction

The current `sensor_spi` example consumes a fixed `nx_spi_endpoint_t` through
`Nexus::BMP280SPI`. `nx_spi_endpoint_transfer()` is CPU-active bounded polling;
it handles complete CS transactions and returns only after hardware and caller
buffers are released. It does not pretend to provide DMA, queued ownership or
asynchronous completion. Optional bus-owner adapters are separate components.

The Native fixture uses a seven-bit register command bank: command bit 7 selects
read and the lower seven bits select a register. It verifies device identity,
calibration and Bosch's reference temperature/pressure. This is an explicit
register model with no physical sensor and no background transfer producer.

MCU sensor deployment requires a reviewed Board binding for SPI signals and CS,
a confirmed sensor model/voltage, transaction budget and electrical qualification.
The current three LED/console assemblies select no sensor endpoint; no Discovery
SPI loopback qualification from the removed architecture is reused here.
