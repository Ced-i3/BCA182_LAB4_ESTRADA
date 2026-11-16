#ifndef SMARTGUARD_SENSORS_H
#define SMARTGUARD_SENSORS_H
#include <stdint.h>

typedef enum { SENSOR_OK, SENSOR_NACK, SENSOR_TIMEOUT, SENSOR_BUS_STUCK, SENSOR_ID, SENSOR_CONFIG, SENSOR_OVERFLOW } sensor_status_t;
typedef struct { int16_t accel[3]; int16_t gyro[3]; } imu_sample_t;
typedef struct { uint16_t light_raw; uint16_t proximity_raw; uint8_t object; } optical_sample_t;
void sensor_bus_init(void);
sensor_status_t imu_init(uint8_t *who);
sensor_status_t imu_read(imu_sample_t *sample);
sensor_status_t optical_init(void);
sensor_status_t optical_read(optical_sample_t *sample);
const char *sensor_status_name(sensor_status_t status);
#endif
