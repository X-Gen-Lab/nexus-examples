# Application-owned component examples

`platform_reference` chooses only LED and UART. Its FreeRTOS worker uses static
TCB/stack, and starts the scheduler explicitly. Bare-metal uses a serialized
loop. Native injects a finite `ping` and checks GPIO and UART completion before
shutdown. It treats EMPTY as ordinary idle and explicitly stops on damaged RX.

`sensor_spi` composes `Nexus::BMP280SPI` with a fixed SPI endpoint. The endpoint
completes each short polling transaction before returning caller buffers and
CS. Native fixtures supply chip ID/calibration/raw measurements and check the
Bosch reference result. No worker, provider discovery or heap is introduced.

`journal` composes generic storage and the optional file-Flash adapter. The
application selects geometry, two independent storage areas, record bytes and
file path. It closes and reopens the file to check persistence recovery. Product
partitioning and physical brownout/erase scheduling remain product work.

`modbus_registers` owns an eight-register bank, transport request/event memory,
modeled RS485 direction and write authorization. An ordinary register write is
accepted; a protected register write without the application interlock receives
an exception and leaves the bank unchanged. The example checks actual UART TC,
wire-response bytes/CRC, bounded synchronous memory logging and clean shutdown.
Direction is a model invariant, not physical transceiver qualification.

None of the components is started implicitly by the platform. Applications
choose resource budgets, polling frequency, tasks, health policy and teardown.
Current sensor/journal/protocol applications execute only on Native because no
corresponding sensor/partition/RS485 physical configuration has been accepted.

[`industrial_control`](../apps/industrial_control/README.md) combines these
mechanisms in one finite Native reference. One control task owns Modbus UART,
sensor policy, Settings and health; a real SPI owner runs separately. Persistent
copies protect BMP280 stack buffers, and completion/timer/stop share one wait
port. Maintenance commit follows stop/drain/join. The nine maintained presets
remain; physical wiring and Flash reservations are still unverified.

[`industrial_control`](../apps/industrial_control/README.md) 将上述机制组合为一个
有限 Native 参考：控制任务拥有 Modbus UART、采集策略、Settings 与健康判断，
SPI owner 独立执行；持久复制保护 BMP280 栈，完成、timer 和 stop 共享 wait port。
维护 commit 在 stop/drain/join 后进行。保持九个受维护 preset，未验收实板接线或
Flash 分区。
