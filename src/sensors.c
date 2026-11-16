#include "sensors.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define SDA GPIO_PIN_0
#define SCL GPIO_PIN_1
#define PORT GPIOF
#define IMU_ADDR 0x68U
#define AP_ADDR 0x1eU

/* The sensor task is the sole bus owner. PF0/PF1 require external pull-ups. */
static void pause_bus(void) {
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < SystemCoreClock / 100000U) {}
}
static void set_line(uint16_t pin, GPIO_PinState value) { HAL_GPIO_WritePin(PORT, pin, value); pause_bus(); }
static sensor_status_t clock_high(void) {
    HAL_GPIO_WritePin(PORT, SCL, GPIO_PIN_SET);
    uint32_t start = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(PORT, SCL) == GPIO_PIN_RESET) {
        if ((uint32_t)(DWT->CYCCNT - start) >= SystemCoreClock / 1000U) return SENSOR_TIMEOUT;
    }
    pause_bus(); return SENSOR_OK;
}
static sensor_status_t stop(void) {
    set_line(SDA, GPIO_PIN_RESET);
    sensor_status_t s = clock_high();
    HAL_GPIO_WritePin(PORT, SDA, GPIO_PIN_SET); pause_bus();
    return s;
}
static sensor_status_t start(int repeated) {
    set_line(SDA, GPIO_PIN_SET);
    sensor_status_t s = clock_high(); if (s != SENSOR_OK) return s;
    if (HAL_GPIO_ReadPin(PORT, SDA) == GPIO_PIN_RESET) return SENSOR_BUS_STUCK;
    set_line(SDA, GPIO_PIN_RESET); set_line(SCL, GPIO_PIN_RESET);
    (void)repeated; return SENSOR_OK;
}
static sensor_status_t write_byte(uint8_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        set_line(SDA, (value & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        sensor_status_t s = clock_high(); if (s != SENSOR_OK) return s;
        set_line(SCL, GPIO_PIN_RESET); value <<= 1;
    }
    set_line(SDA, GPIO_PIN_SET);
    sensor_status_t s = clock_high(); if (s != SENSOR_OK) return s;
    int ack = HAL_GPIO_ReadPin(PORT, SDA) == GPIO_PIN_RESET;
    set_line(SCL, GPIO_PIN_RESET); return ack ? SENSOR_OK : SENSOR_NACK;
}
static sensor_status_t read_byte(uint8_t *value, int last) {
    uint8_t byte = 0; set_line(SDA, GPIO_PIN_SET);
    for (unsigned i = 0; i < 8; ++i) {
        sensor_status_t s = clock_high(); if (s != SENSOR_OK) return s;
        byte = (uint8_t)((byte << 1) | (HAL_GPIO_ReadPin(PORT, SDA) == GPIO_PIN_SET));
        set_line(SCL, GPIO_PIN_RESET);
    }
    set_line(SDA, last ? GPIO_PIN_SET : GPIO_PIN_RESET);
    sensor_status_t s = clock_high(); if (s != SENSOR_OK) return s;
    set_line(SCL, GPIO_PIN_RESET); set_line(SDA, GPIO_PIN_SET); *value = byte; return SENSOR_OK;
}
static sensor_status_t transfer(uint8_t addr, uint8_t reg, uint8_t *data, unsigned length, int read) {
    sensor_status_t s = start(0);
    if (s == SENSOR_OK) s = write_byte((uint8_t)(addr << 1));
    if (s == SENSOR_OK) s = write_byte(reg);
    if (s == SENSOR_OK && read) s = start(1);
    if (s == SENSOR_OK && read) s = write_byte((uint8_t)((addr << 1) | 1U));
    for (unsigned i = 0; s == SENSOR_OK && i < length; ++i)
        s = read ? read_byte(&data[i], i + 1U == length) : write_byte(data[i]);
    sensor_status_t end = stop();
    return s == SENSOR_OK ? end : s;
}
static sensor_status_t rd(uint8_t a, uint8_t r, uint8_t *v, unsigned n) { return transfer(a, r, v, n, 1); }
static sensor_status_t wr(uint8_t a, uint8_t r, uint8_t v) { return transfer(a, r, &v, 1, 0); }
void sensor_bus_init(void) {
    GPIO_InitTypeDef g = {0}; __HAL_RCC_GPIOF_CLK_ENABLE();
    HAL_GPIO_WritePin(PORT, SDA | SCL, GPIO_PIN_SET);
    g.Pin = SDA | SCL; g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(PORT, &g);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static sensor_status_t write_check(uint8_t a, uint8_t reg, uint8_t value, uint8_t mask) {
    uint8_t got = 0; sensor_status_t s = wr(a, reg, value);
    if (s == SENSOR_OK) s = rd(a, reg, &got, 1);
    return s != SENSOR_OK ? s : ((got & mask) == (value & mask) ? SENSOR_OK : SENSOR_CONFIG);
}
sensor_status_t imu_init(uint8_t *who) {
    *who = 0; sensor_status_t s = rd(IMU_ADDR, 0x75, who, 1);
    if (s != SENSOR_OK) return s;
    if (*who != 0xafU && *who != 0xaeU) return SENSOR_ID;
    s = write_check(IMU_ADDR, 0x6b, 0x01, 0x47); /* wake, PLL clock */
    if (s != SENSOR_OK) return s;
    vTaskDelay(pdMS_TO_TICKS(100));
    s = write_check(IMU_ADDR, 0x6c, 0x00, 0x3f); /* all axes enabled */
    if (s == SENSOR_OK) s = write_check(IMU_ADDR, 0x1b, 0x00, 0x18); /* +/-250 dps */
    if (s == SENSOR_OK) s = write_check(IMU_ADDR, 0x1c, 0x00, 0x18); /* +/-2 g */
    return s;
}
static int16_t be16(const uint8_t *p) { return (int16_t)(((uint16_t)p[0] << 8) | p[1]); }
sensor_status_t imu_read(imu_sample_t *sample) {
    uint8_t b[14]; sensor_status_t s = rd(IMU_ADDR, 0x3b, b, sizeof b);
    if (s == SENSOR_OK) for (unsigned i = 0; i < 3; ++i) { sample->accel[i] = be16(&b[2*i]); sample->gyro[i] = be16(&b[8+2*i]); }
    return s;
}
sensor_status_t optical_init(void) {
    sensor_status_t s = write_check(AP_ADDR, 0x10, 0x00, 0x30); /* 23360 lux range */
    if (s == SENSOR_OK) s = write_check(AP_ADDR, 0x00, 0x03, 0x07); /* ALS and PS */
    if (s == SENSOR_OK) vTaskDelay(pdMS_TO_TICKS(250));
    return s;
}
sensor_status_t optical_read(optical_sample_t *sample) {
    uint8_t b[4]; sensor_status_t s = SENSOR_OK;
    /* Read each low byte before its latched high byte; do not assume auto-increment. */
    for (unsigned i = 0; i < 4 && s == SENSOR_OK; ++i) s = rd(AP_ADDR, (uint8_t)(0x0cU + i), &b[i], 1);
    if (s != SENSOR_OK) return s;
    sample->light_raw = (uint16_t)(((uint16_t)b[1] << 8) | b[0]);
    sample->proximity_raw = (uint16_t)(((uint16_t)(b[3] & 0x3fU) << 4) | (b[2] & 0x0fU));
    sample->object = (b[2] >> 7) & 1U;
    return (b[2] & 0x40U) ? SENSOR_OVERFLOW : SENSOR_OK;
}
const char *sensor_status_name(sensor_status_t s) {
    static const char *const names[] = {"ok", "nack", "timeout", "bus_stuck", "id_mismatch", "config_mismatch", "ir_overflow"};
    return (unsigned)s < sizeof names / sizeof names[0] ? names[s] : "unknown";
}
