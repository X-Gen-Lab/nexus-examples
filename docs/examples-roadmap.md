# Next application iterations

The authored baseline now has eight finite Native applications, registered host
Google programs, a real FreeRTOS POSIX SPI composition, seventeen main presets, four
versioned starter templates, and standalone F407 SPI/H743 continuous receive
software fixtures. See [developer recipes](developer-recipes.md) for exact
lifecycle and evidence boundaries. Qualification requires the coordinated clean
SDK pin and actual ordinary gates; authored targets alone are not execution.

当前工程包含八个有限 Native 应用、已注册 Google 程序、真实 FreeRTOS POSIX SPI
组合、十七个主 preset、四个版本化最小工程及独立 F407 SPI/H743 连续接收软件
fixture。已编写的目标不等于已执行验收；最终证据必须对应同一干净 SDK pin。

The [industrial reference](../apps/industrial_control/README.md) now provides
the Native sampling/protocol/persistence/health composition and a second real
completion/timer/stop consumer. Its next hardware step requires reviewed
sensor/RS485 wiring and product Flash regions, not additional product frameworks.

[工业参考](../apps/industrial_control/README.md) 已提供 Native 采集、协议、持久化与
健康组合，以及第二个真实 completion/timer/stop 消费者。后续硬件工作需要审定
传感器/RS485 接线和产品 Flash 区域，不增加产品框架。

| Next step | Required inputs | Acceptance |
| --- | --- | --- |
| Connected Board HIL | Board revision, probe/serial identity, safe wiring | Real boot, IRQ load, timing, UART loss and teardown |
| Physical SPI sensor | Reviewed CS/signals, sensor voltage and datasheet | Register identity, known calibration, worst-load bound |
| Industrial RS485 | Transceiver DE/RE, bus topology, termination and protocol budget | TC-before-DE release, silence gaps, loss/recovery |
| MCU journal | Product-owned Flash areas and maintenance windows | Power-cut recovery, erase/program interference budget |
| More physical owner integrations | Reviewed controller/CS wiring and product traffic | Reuse the two-endpoint software recipe; validate actual arbitration, cancellation and final borrow return |
| Explicit fault recovery | First-fault record, retained loans, reviewed output/reset port | Failed actions retain storage; actual output/reset observations are independently qualified |
| Device fleet integration | Product identity, threat model, transport and update contract | Verified recovery and authenticated update evidence |

Each extension belongs to its application/product repository. Generic provider
or contract defects are repaired in Nexus with focused fault tests; example
caller/pin changes are reviewed separately. HIL, networks, manufacturing and
update policies are not inferred from the existence of a host example.

The superseded plans and their task counts remain historical context in
`platform-refactor-plan.md`. Follow the selected SDK's `docs/delivery/` support,
qualification and iteration documents for generic platform work. A planned or
implemented Core feature does not add an example preset, a reviewed PCB route
or physical qualification. This repository tracks its caller-specific work and
integration separately.
