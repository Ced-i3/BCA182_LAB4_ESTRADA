#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include "sensors.h"

static UART_HandleTypeDef uart;
void xPortSysTickHandler(void);
static void fail(void) { __disable_irq(); for (;;) {} }
void vApplicationMallocFailedHook(void) { fail(); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; (void)name; fail(); }
void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) xPortSysTickHandler();
}
static void clock_init(void) {
    RCC_OscInitTypeDef osc = {0}; RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE; osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON; osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8; osc.PLL.PLLN = 336; osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fail();
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4; clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) fail();
}
static void peripherals_init(void) {
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOF_CLK_ENABLE(); __HAL_RCC_USART1_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_11 | GPIO_PIN_12; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOF, &g);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5; g.Mode = GPIO_MODE_INPUT; g.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_9 | GPIO_PIN_10; g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH; g.Alternate = GPIO_AF7_USART1; HAL_GPIO_Init(GPIOA, &g);
    uart.Instance = USART1; uart.Init.BaudRate = 115200; uart.Init.WordLength = UART_WORDLENGTH_8B; uart.Init.StopBits = UART_STOPBITS_1; uart.Init.Parity = UART_PARITY_NONE; uart.Init.Mode = UART_MODE_TX_RX; uart.Init.HwFlowCtl = UART_HWCONTROL_NONE; uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&uart) != HAL_OK) fail();
    sensor_bus_init();
}
static TaskHandle_t heartbeat_handle, button_handle, sensor_handle;
static volatile uint8_t button_state;
static void heartbeat_task(void *arg) {
    (void)arg; TickType_t wake = xTaskGetTickCount();
    for (;;) { HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_11); vTaskDelayUntil(&wake, pdMS_TO_TICKS(500)); }
}
static void button_task(void *arg) {
    (void)arg; TickType_t wake = xTaskGetTickCount();
    for (;;) {
        uint32_t pins = GPIOC->IDR;
        button_state = (uint8_t)(((pins & GPIO_PIN_5) == 0U ? 1U : 0U) |
                                 ((pins & GPIO_PIN_1) == 0U ? 2U : 0U) |
                                 ((pins & GPIO_PIN_0) == 0U ? 4U : 0U) |
                                 ((pins & GPIO_PIN_4) == 0U ? 8U : 0U));
        HAL_GPIO_WritePin(GPIOF, GPIO_PIN_12, button_state ? GPIO_PIN_RESET : GPIO_PIN_SET);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
    }
}
static void send_line(const char *text) {
    size_t length = strlen(text);
    if (length) (void)HAL_UART_Transmit(&uart, (const uint8_t *)text, (uint16_t)length, 100);
}
/* Fixed-point formatting avoids requiring float printf support. */
static void fixed3(char *out, size_t n, int32_t value) {
    uint32_t mag = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    (void)snprintf(out, n, "%s%lu.%03lu", value < 0 ? "-" : "", (unsigned long)(mag / 1000U), (unsigned long)(mag % 1000U));
}
static void sensor_task(void *arg) {
    (void)arg;
    TickType_t wake = xTaskGetTickCount(), previous = wake;
    unsigned cycle = 0; uint8_t last_buttons = 0xffU; int imu_ready = 0, ap_ready = 0;
    uint8_t who = 0; sensor_status_t is = SENSOR_CONFIG, as = SENSOR_CONFIG;
    char linebuf[220], ax[16], ay[16], az[16], gx[16], gy[16], gz[16], lux[16];
    send_line("SmartGuard sensor diagnostics; tick_ms=1 period_ms=500; values uncalibrated\r\n");
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        unsigned long elapsed = (unsigned long)((now - previous) * portTICK_PERIOD_MS); previous = now;
        /* Reinitialize failed devices at a bounded 2.5-second interval. */
        if (!imu_ready && cycle % 5U == 0U) { is = imu_init(&who); imu_ready = is == SENSOR_OK; }
        if (!ap_ready && cycle % 5U == 0U) { as = optical_init(); ap_ready = as == SENSOR_OK; }
        imu_sample_t imu; optical_sample_t optical;
        if (imu_ready) { is = imu_read(&imu); if (is != SENSOR_OK) imu_ready = 0; }
        if (ap_ready) { as = optical_read(&optical); if (as != SENSOR_OK && as != SENSOR_OVERFLOW) ap_ready = 0; }
        if (is == SENSOR_OK && imu_ready) {
            fixed3(ax, sizeof ax, (int32_t)imu.accel[0]*1000/16384); fixed3(ay, sizeof ay, (int32_t)imu.accel[1]*1000/16384); fixed3(az, sizeof az, (int32_t)imu.accel[2]*1000/16384);
            fixed3(gx, sizeof gx, (int32_t)imu.gyro[0]*1000/131); fixed3(gy, sizeof gy, (int32_t)imu.gyro[1]*1000/131); fixed3(gz, sizeof gz, (int32_t)imu.gyro[2]*1000/131);
            snprintf(linebuf, sizeof linebuf, "t_ms=%lu dt_ms=%lu IMU who=0x%02X accel_g=%s,%s,%s gyro_dps=%s,%s,%s\r\n", (unsigned long)(now * portTICK_PERIOD_MS), elapsed, who, ax, ay, az, gx, gy, gz);
        } else snprintf(linebuf, sizeof linebuf, "t_ms=%lu dt_ms=%lu IMU invalid=%s who=0x%02X\r\n", (unsigned long)(now * portTICK_PERIOD_MS), elapsed, sensor_status_name(is), who);
        send_line(linebuf);
        if (as == SENSOR_OK || as == SENSOR_OVERFLOW) {
            /* 23360 lux full scale / 65536 counts, rounded to millilux. */
            fixed3(lux, sizeof lux, (int32_t)(((uint32_t)optical.light_raw * 23360000ULL) / 65536U));
            if (as == SENSOR_OK) snprintf(linebuf, sizeof linebuf, "t_ms=%lu AP lux=%s als_raw=%u prox_raw=%u object=%u\r\n", (unsigned long)(now * portTICK_PERIOD_MS), lux, optical.light_raw, optical.proximity_raw, optical.object);
            else snprintf(linebuf, sizeof linebuf, "t_ms=%lu AP lux=%s als_raw=%u prox=invalid:%s\r\n", (unsigned long)(now * portTICK_PERIOD_MS), lux, optical.light_raw, sensor_status_name(as));
        } else snprintf(linebuf, sizeof linebuf, "t_ms=%lu AP invalid=%s\r\n", (unsigned long)(now * portTICK_PERIOD_MS), sensor_status_name(as));
        send_line(linebuf);
        uint8_t buttons = button_state;
        if (cycle % 10U == 0U || buttons != last_buttons) {
            last_buttons = buttons;
            snprintf(linebuf, sizeof linebuf, "t_ms=%lu buttons up=%u down=%u left=%u right=%u stack_words heartbeat=%lu buttons=%lu sensor=%lu heap_free=%lu heap_min=%lu\r\n", (unsigned long)(now * portTICK_PERIOD_MS), !!(buttons&1U), !!(buttons&2U), !!(buttons&4U), !!(buttons&8U), (unsigned long)uxTaskGetStackHighWaterMark(heartbeat_handle), (unsigned long)uxTaskGetStackHighWaterMark(button_handle), (unsigned long)uxTaskGetStackHighWaterMark(sensor_handle), (unsigned long)xPortGetFreeHeapSize(), (unsigned long)xPortGetMinimumEverFreeHeapSize());
            send_line(linebuf);
        }
        ++cycle; vTaskDelayUntil(&wake, pdMS_TO_TICKS(500));
    }
}
int main(void) {
    HAL_Init(); clock_init(); peripherals_init();
    if (xTaskCreate(heartbeat_task, "heartbeat", 256, NULL, 1, &heartbeat_handle) != pdPASS ||
        xTaskCreate(button_task, "buttons", 256, NULL, 2, &button_handle) != pdPASS ||
        xTaskCreate(sensor_task, "sensors", 768, NULL, 1, &sensor_handle) != pdPASS) fail();
    vTaskStartScheduler(); fail();
}
