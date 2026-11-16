#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

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
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOF, &g);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static void bus_delay(void) { uint32_t start = DWT->CYCCNT; while ((uint32_t)(DWT->CYCCNT - start) < SystemCoreClock / 100000U) {} }
static void line(uint16_t pin, GPIO_PinState state) { HAL_GPIO_WritePin(GPIOF, pin, state); bus_delay(); }
static int scl_high(void) {
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_1, GPIO_PIN_SET);
    uint32_t start = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_1) == GPIO_PIN_RESET) if ((uint32_t)(DWT->CYCCNT - start) > SystemCoreClock / 1000U) return 0;
    bus_delay(); return 1;
}
/* Address ACK only: it does not establish device identity. */
static int probe(uint8_t address) {
    line(GPIO_PIN_0, GPIO_PIN_SET); if (!scl_high() || HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_0) == GPIO_PIN_RESET) return -1;
    line(GPIO_PIN_0, GPIO_PIN_RESET); line(GPIO_PIN_1, GPIO_PIN_RESET);
    uint8_t byte = (uint8_t)(address << 1);
    for (int i = 0; i < 8; ++i) {
        line(GPIO_PIN_0, (byte & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        if (!scl_high()) return -1;
        line(GPIO_PIN_1, GPIO_PIN_RESET); byte <<= 1;
    }
    line(GPIO_PIN_0, GPIO_PIN_SET); if (!scl_high()) return -1;
    int ack = HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_0) == GPIO_PIN_RESET;
    line(GPIO_PIN_1, GPIO_PIN_RESET); line(GPIO_PIN_0, GPIO_PIN_RESET);
    if (!scl_high()) return -1;
    line(GPIO_PIN_0, GPIO_PIN_SET); return ack;
}
static void heartbeat_task(void *arg) { (void)arg; for (;;) { HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_11); vTaskDelay(pdMS_TO_TICKS(500)); } }
static void button_task(void *arg) { (void)arg; for (;;) { GPIO_PinState pressed = (GPIOC->IDR & (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5)) != (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5) ? GPIO_PIN_RESET : GPIO_PIN_SET; HAL_GPIO_WritePin(GPIOF, GPIO_PIN_12, pressed); vTaskDelay(pdMS_TO_TICKS(50)); } }
static void sensor_task(void *arg) { (void)arg; char msg[120]; for (;;) {
    int ap = probe(0x1e), imu68 = probe(0x68), imu69 = probe(0x69);
    int n = snprintf(msg, sizeof msg, "SmartGuard ACK AP@1E=%d IMU@68=%d IMU@69=%d (-1=bus error)\r\n", ap, imu68, imu69);
    if (n > 0 && n < (int)sizeof msg) HAL_UART_Transmit(&uart, (uint8_t *)msg, (uint16_t)n, 100);
    vTaskDelay(pdMS_TO_TICKS(2000));
} }
int main(void) {
    HAL_Init(); clock_init(); peripherals_init();
    if (xTaskCreate(heartbeat_task, "heartbeat", 256, NULL, 1, NULL) != pdPASS ||
        xTaskCreate(button_task, "buttons", 256, NULL, 2, NULL) != pdPASS ||
        xTaskCreate(sensor_task, "sensors", 512, NULL, 1, NULL) != pdPASS) fail();
    vTaskStartScheduler(); fail();
}
