# Maintained reference boards

| Board | Exact MCU | Selected clock source | Example LED | Console |
| --- | --- | --- | --- | --- |
| 启明欣欣 V3.1 | STM32F407ZGT6 | 8 MHz | PE3, low active | USART1 PA9/PA10 |
| 天空星青春版 | STM32F407VET6 | 8 MHz | PB2, high active | USART1 PA9/PA10 |
| 梁山派 | GD32F470ZGT6 | 25 MHz | PD7, high active | USART0 PA9/PA10 |
| NUCLEO-H743ZI2 software reference | STM32H743ZIT6 | internal HSI 64 MHz | PB0, high active | USART3 PD8/PD9 AF7 |
| NUCLEO-U575ZI-Q software reference | STM32U575ZIT6Q | internal MSIS 4 MHz | PC7, high active | USART1 PA9/PA10 AF7 |
| NUCLEO-F103RB source reference | STM32F103RBT6 | internal HSI 8 MHz | PA5 LD2 source reference | USART2 PA2/PA3 |
| GD32F303RC silicon fixture | GD32F303RCT6 | internal IRC 8 MHz | PA5 software output; no PCB/LED asserted | USART0 PA9/PA10 |
| G474RE HSE24 source reference | STM32G474RET6 | 24 MHz source reference | PA5 LD2 source reference | LPUART1 PA2/PA3 AF12 |

These facts come from reviewed SDK Board packages and their source references.
They have not been compared with connected physical PCB revisions or fitted
options. STM32 SWD PA13/PA14 remains reserved. The GD32 monotonic clock reserves
TIMER1. Board defaults set reviewed inactive LED levels before application use.

Each reference has bare-metal and FreeRTOS assemblies. They select a 115200 baud
console with an exact 32-event RX budget on the original three boards, or a
32-byte RX budget on the other five software references. IRQ priorities and
static memory budgets are explicit in each assembly. UART typed providers use IRQ TX,
not DMA. RX loss/status-only events must never be interpreted as fabricated bytes.

The H743 reference requires the reviewed MB1364 LED1 PB0 path (SB39 ON/SB47
OFF), USART3 VCP path (SB19/SB12), LDO supply entry and cold HSI64 Cache-off state.
The U575 reference requires the reviewed MB1549 PC7 LD1 path (SB21 ON/SB23 OFF),
USART1 VCP path (SB32/SB34 ON; SB24/SB26/SB31/SB33 OFF), TZEN0 and SWAP_BANK0.
Actual PCB, regulator/SMPS fitting, bridges and option bytes remain unobserved.
The example never modifies option bytes. Both references reserve TIM2 for time
and maintain only the declared GPIO and IRQ-byte UART routes; DMA, cache,
TrustZone, deep STOP and running-state handoff are outside their current scope.

The shared reference RX policy calls the public event operation first. Only an
UNSUPPORTED result selects the public byte operation. A damaged event or byte
overflow returns failure with zero bytes authorized for echo. The UART owner
application uses the same public TX, IRQ hint and settlement contracts across
all eight software references. FreeRTOS assemblies explicitly permit the
configured priority-5
UART hint to call the kernel; bare-metal hints do not call it.

Only these declared GPIO/console routes are exercised by the MCU reference
application. F103RB, GD32F303RC and G474RE fittings, loads, supply and serial
connections remain unobserved; their compiled routes do not establish PCB wiring
or physical LEDs. GD32F303RC is a silicon fixture with no selected development
board.

Only selected routes are composed by each application.
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
