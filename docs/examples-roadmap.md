# Next application iterations

The immediate baseline is four finite Native examples plus the three-board,
two-backend LED/console firmware. Preserve a repeatable clean source pin before
adding more capabilities.

| Next step | Required inputs | Acceptance |
| --- | --- | --- |
| Connected Board HIL | Board revision, probe/serial identity, safe wiring | Real boot, IRQ load, timing, UART loss and teardown |
| Physical SPI sensor | Reviewed CS/signals, sensor voltage and datasheet | Register identity, known calibration, worst-load bound |
| Industrial RS485 | Transceiver DE/RE, bus topology, termination and protocol budget | TC-before-DE release, silence gaps, loss/recovery |
| MCU journal | Product-owned Flash areas and maintenance windows | Power-cut recovery, erase/program interference budget |
| Multi-controller owner use | Explicit queue budget, producers and cancel identity | Full queue, stale epoch, absolute deadline, final borrow return |
| Device fleet integration | Product identity, threat model, transport and update contract | Verified recovery and authenticated update evidence |

Each extension belongs to its application/product repository. Generic provider
or contract defects are repaired in Nexus with focused fault tests; example
caller/pin changes are reviewed separately. HIL, networks, manufacturing and
update policies are not inferred from the existence of a host example.

The superseded 38-task plan is retained in `platform-refactor-plan.md` only as
historical context. Current Nexus implementation planning is the SDK
`docs/design/next-generation-execution.csv` with 33 explicit software tasks.
