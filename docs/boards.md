# Maintained reference boards

| Board | Exact MCU | Reviewed HSE | Example LED | Console |
| --- | --- | --- | --- | --- |
| 启明欣欣 V3.1 | STM32F407ZGT6 | 8 MHz | PE3, low active | USART1 PA9/PA10 |
| 天空星青春版 | STM32F407VET6 | 8 MHz | PB2, high active | USART1 PA9/PA10 |
| 梁山派 | GD32F470ZGT6 | 25 MHz | PD7, high active | USART0 PA9/PA10 |

These facts come from reviewed SDK Board packages and their source references.
They have not been compared with connected physical PCB revisions or fitted
options. STM32 SWD PA13/PA14 remains reserved. The GD32 monotonic clock reserves
TIMER1. Board defaults set reviewed inactive LED levels before application use.

Each board has bare-metal and FreeRTOS assemblies. They select a 115200 baud
console with an exact 32-event caller-owned RX budget. IRQ priorities and static
memory budgets are explicit in each assembly. UART typed providers use IRQ TX,
not DMA. RX loss/status-only events must never be interpreted as fabricated bytes.

Only these LED/console routes are exercised by the MCU reference application.
The Liangshan package also describes SPI4 PF6 CS and PF7/PF8/PF9, but the fitted
NOR part is unknown; the sensor example is not declared supported there.
Unknown I2C/PWM/ADC/EXTI external wiring is not automatically exposed by examples.

Before HIL, identify board revision, oscillator population, supply, probe,
serial port and connector continuity. UART pins are MCU logic levels: use the
board's documented compatible interface, do not assume RS232 voltages. Confirm
safe output levels and external loads before driving GPIO/PWM.

No board is attached in this session. Flash erase/program, watchdog reset,
RS485 direction, power interruption and worst-load IRQ tests are unexecuted.
The SDK HIL plan/station manifests prepare those checks without inventing a
station or a passing physical result.
