# SmartGuard implementation plan

This is a design and resource allocation, not an implemented or hardware-verified matrix driver. Continue existing board verification and IMU diagnostics before the matrix milestone. No external disco LEDs are planned.

## Current evidence and priority

The Milestone 1 image was built, uploaded through ST-LINK and verified by OpenOCD. COM4 at 115200 baud repeatedly reported `AP@1E=1 IMU@68=1 IMU@69=0`; these are address acknowledgements, not identity checks or measurements. PF11's output register changed between two SWD samples, but visible LED behavior has not been confirmed. Subsequent diagnostics observed IMU WHO_AM_I 0xAF, actual accelerometer and gyro readings, optical response and individual button press/release flags; see [hardware test results](Hardware_Test_2026-11-17.md). Validate timing, calibration and fault behavior further before treating acquisition as reliable. The AP3216C register map examined does not provide a unique chip ID; use documented register behavior and physical inspection without claiming definitive electronic identification. Prepare any firmware changes for separate approval before flashing.

## Verified design references and limits

The [RT-Spark HW1.1 schematic](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/hw-board/spark-1/SCH_Spark-1_V1_0.pdf) shows the MCU, LED matrix, buffer/enable and buzzer circuits; check the actual board revision and fitted parts. The [official board matrix driver](https://github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark/blob/main/libraries/Board_Drivers/led_matrix/drv_matrix_led.c) defaults to 19 LEDs, TIM3 CH2, DMA1 Stream 5, and activates the buffer enable low; its [header](https://github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark/blob/main/libraries/Board_Drivers/led_matrix/drv_matrix_led.h) identifies PF2. The same driver contains example timing and low-brightness colors; confirm the fitted LED part and waveform rather than assuming all WS2812 variants have identical limits. PA7 is TIM3_CH2 (AF2). PA7 is also exposed on a PMOD connector; do not attach a conflicting peripheral to that line.

## Proposed resource allocation

| Function | Resource | Status and constraint |
| --- | --- | --- |
| Matrix data | PA7, AF2, TIM3 CH2, DMA1 Stream 5, channel mapping per STM32F407 reference manual | Dedicated to the matrix; verify DMA request, waveform and GPIO routing on the actual board. |
| Matrix buffer enable | PF2, active low | Buffer output-enable, not a proven LED power switch. Initialize disabled (high), drive a defined low data level, then enable as appropriate. Do not assume disable removes LED power or clears retained colors. |
| Buzzer | PB0, AF1, TIM1 CH2N | Proposed independent PWM; uses advanced timer complementary output and requires correct polarity, idle state, main-output enable and HAL complementary-PWM start. Check transistor polarity and audio behavior. |
| Sensor bus | PF1 SCL / PF0 SDA, software I2C | Keep current sensor-task ownership; establish bounded transactions and reliable IMU data first. |
| Buttons | PC5, PC1, PC0, PC4 | Debounce and publish events; validate actual polarity. |
| LCD | Board FSMC interface and board-specific reset/backlight signals | Confirm controller, pin routing and any timer/DMA use from the board schematic and actual module before allocating; avoid TIM1, TIM3, DMA1 Stream 5 and PA7 conflicts. |
| Serial / RTOS | USART1 PA9/PA10; SysTick, SVC and PendSV | Preserve diagnostics and FreeRTOS interrupt priority rules. |

TIM3 channels share a counter, prescaler and period. Running a buzzer tone on TIM3 CH3 while CH2 generates the approximately 800 kHz LED waveform cannot provide an independently chosen audio frequency. Move buzzer PWM to TIM1 CH2N on PB0 as supported by the [STM32F407 alternate-function table](https://www.st.com/resource/en/datasheet/stm32f407ie.pdf); reserve TIM1 for the buzzer unless future usage is explicitly coordinated. Neither the buzzer nor this alternative has yet been tested.

## Matrix design for a later milestone

- Use STM32Cube HAL timer PWM with DMA, not an RT-Thread runtime or Arduino library. At the planned 84 MHz TIM3 clock, an 800 kHz bit cell is 105 timer counts; select pulse widths and reset/latch low time from the exact fitted LED datasheet and validate with a logic analyzer. Treat these as design values, not measured timing.
- A single matrix task owns TIM3, DMA, PF2, the encoded waveform buffer and state. Other tasks send state requests through a bounded queue or notification; the task coalesces stale display requests, prioritizes alarm state and never modifies a buffer while DMA uses it. Do not disable interrupts for the frame. Keep the buffer in DMA-accessible main SRAM, not CCM.
- DMA completion/error interrupt calls the HAL handler; if using FreeRTOS ISR APIs, configure its numerical NVIC priority at or below the permitted syscall urgency (library priority 5 or greater here), use FromISR APIs and yield correctly. On completion, ensure data remains low for the LED's required latch interval before another frame; track DMA busy, timeout and failure, and transition safely without blocking other tasks.
- At startup, keep PF2 high while PA7 is configured low. Once enabled, send an all-zero frame and complete the reset interval before declaring the matrix off. On error do not assume changing PF2 clears the LEDs; report the fault.
- Start with a conservative per-channel cap of 16/255, including white, and clamp every frame centrally. This is a software starting limit, not a measured current guarantee; verify LED supply, USB/board current limits, total current and heating. Avoid full-brightness white. LED idle current and nonlinear behavior mean PWM scaling is not a substitute for measuring current.

| State | Planned display |
| --- | --- |
| DISARMED | All pixels off. |
| ARMED | Steady low-brightness green. |
| ALARM | Alternate low-brightness red and white using task timing; no busy-wait. |
| DIAGNOSTICS | Bounded red, green and blue sequence, then restore the requested state. |

ALARM is a design state only; do not enable product alarm behavior during current sensor bring-up. Define precedence and interruptions explicitly when the application state machine is implemented.

## Milestone order and verification

1. Finish visible heartbeat observations and reliable sensor diagnostics: validate calibration, timing and failures and record actual evidence. Preserve the current scope and request approval for each modified firmware upload.
2. Separately implement the matrix driver and TIM1 buzzer allocation with small, meaningful commits. First build and review, then obtain approval before flashing. Verify LED order, colors, off/startup behavior, pulse timing and latch interval, DMA errors and task stack margins. Measure power at capped brightness.
3. Exercise matrix, buzzer, LCD, buttons and sensor acquisition together. Confirm LCD routing and timer/DMA resource ownership, task responsiveness and timing under simultaneous operation, fault behavior, and no regressions in the sensor data. Only claim coexistence after physical testing.
4. Continue product and ML work only after sensor diagnostics and this independent matrix milestone are reliable. No model training is part of this plan update.
