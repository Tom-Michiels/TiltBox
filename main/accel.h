#ifndef ACCEL_H
#define ACCEL_H

#include <stdint.h>
#include "esp_err.h"

void i2c_init(void);
esp_err_t adxl345_init(void);
esp_err_t adxl345_read(int16_t *x, int16_t *y, int16_t *z);
void calibrate_accel(void);

extern int16_t cal_x, cal_y;

#endif // ACCEL_H
