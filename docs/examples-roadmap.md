# Next application iterations

The immediate baseline is five finite Native examples, complete lifecycle and
fault-policy Google execution, plus three-board/two-backend platform and UART
owner firmware. First reconcile the new public fault API with the coordinated
clean SDK pin and execute the complete matrix. Preserve that source identity
before adding more capabilities.

| Next step | Required inputs | Acceptance |
| --- | --- | --- |
| Connected Board HIL | Board revision, probe/serial identity, safe wiring | Real boot, IRQ load, timing, UART loss and teardown |
| Physical SPI sensor | Reviewed CS/signals, sensor voltage and datasheet | Register identity, known calibration, worst-load bound |
| Industrial RS485 | Transceiver DE/RE, bus topology, termination and protocol budget | TC-before-DE release, silence gaps, loss/recovery |
| MCU journal | Product-owned Flash areas and maintenance windows | Power-cut recovery, erase/program interference budget |
| Multi-controller owner use | Explicit queue budget, producers and cancel identity | Full queue, stale epoch, absolute deadline, final borrow return |
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
