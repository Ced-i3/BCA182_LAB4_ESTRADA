# SmartGuard Milestone 1

## Build

PlatformIO, STM32Cube HAL, and ST-LINK are configured in `platformio.ini`. With PlatformIO on PATH, run `pio run`. On this machine PlatformIO Core 6.2.0 was run from `%USERPROFILE%\.platformio\penv\Scripts\platformio.exe` with `PLATFORMIO_CORE_DIR` temporarily set to `%USERPROFILE%\.platformio`; the inherited `C:\PIO_TOOLS` location denied lock-file access in the sandbox. Installed platform ststm32 19.7.1, STM32CubeF4 1.28.1 and GCC ARM 7.2.1 built successfully. The framework package supplies the FreeRTOS kernel; no downloaded library or cloud service is required. Build output: 10,240 bytes flash and 16,876 bytes main SRAM. No firmware was flashed.

The board manifest targets STM32F407ZGT6, 1 MiB flash, 128 KiB main SRAM. The chip also has 64 KiB CCM, which the default linker memory region does not include. The MCU match does not imply generic board pin compatibility.

## Board configuration

- HSE assumed 8 MHz as specified for this project; PLL M=8, N=336, P=2, Q=7; SYSCLK/AHB 168 MHz, APB1 42 MHz, APB2 84 MHz. Confirm the physical board oscillator/revision.
- USART1: PA9 TX, PA10 RX, 115200 8N1. Board BSP identifies UART1 as USB-to-serial.
- LEDs: PF11 (LED_B), PF12 (LED_R). Current code assumes active-low; verify polarity on hardware.
- Buttons: PC5 up, PC1 down, PC0 left, PC4 right; inputs use pull-ups and assume pressed low. Verify polarity and board revision.
- Both sensors: software I2C SCL PF1, SDA PF0, open drain. External pull-ups are required; verify board circuitry. AP3216C address 0x1e; ICM20608 candidates 0x68 and 0x69. The diagnostic only tests address ACK, not device identity or sensor readings. -1 means bus error, 0 means no ACK, 1 means ACK. The bus supports clock-stretch timeout, but has no stuck-bus recovery.

The FreeRTOS GCC ARM_CM4F port uses a 1 kHz tick, 16 KiB heap_4 heap, and stack-overflow/malloc-failure hooks. Three tasks independently run the LED heartbeat (500 ms), button polling (50 ms), and sensor probing/serial output (2 s). HAL and FreeRTOS share SysTick. Button handling is only a basic level indication, without debouncing.

## Hardware validation still required

After separate authorization to flash, verify board revision, oscillator, clock and UART baud; LED and button polarity; independent task timing; stack margins; I2C pull-ups and waveforms; and sensor ACKs and actual identities. The current probes do not prove which device acknowledged. Verify bus behavior when a sensor is absent or a line is held low. No hardware tests were performed.

Sources: [RT-Thread Spark BSP](https://github.com/RT-Thread/rt-thread/blob/master/bsp/stm32/stm32f407-rt-spark/README_EN.md), [board BSP sensor configuration](https://github.com/RT-Thread/rt-thread/blob/master/bsp/stm32/stm32f407-rt-spark/board/Kconfig), [Spark schematic](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/hw-board/spark-1/SCH_Spark-1_V1_0.pdf).
