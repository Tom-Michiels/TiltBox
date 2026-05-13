#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "accel.h"

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         GPIO_NUM_8
#define I2C_SCL         GPIO_NUM_9
#define I2C_FREQ_HZ     100000

#define ADXL345_ADDR        0x53
#define ADXL345_DEVID       0x00
#define ADXL345_BW_RATE     0x2C
#define ADXL345_POWER_CTL   0x2D
#define ADXL345_DATA_FORMAT 0x31
#define ADXL345_DATAX0      0x32

int16_t cal_x = 0, cal_y = 0;

void i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t adxl345_write_reg(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t adxl345_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t adxl345_init(void)
{
    uint8_t devid;
    esp_err_t ret = adxl345_read_reg(ADXL345_DEVID, &devid, 1);
    if (ret != ESP_OK) return ret;
    if (devid != 0xE5) {
        printf("ADXL345 wrong ID: 0x%02X (expected 0xE5)\n", devid);
        return ESP_ERR_NOT_FOUND;
    }

    ret = adxl345_write_reg(ADXL345_BW_RATE, 0x0A);
    if (ret != ESP_OK) return ret;

    ret = adxl345_write_reg(ADXL345_DATA_FORMAT, 0x09);
    if (ret != ESP_OK) return ret;

    ret = adxl345_write_reg(ADXL345_POWER_CTL, 0x08);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t adxl345_read(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6];
    esp_err_t ret = adxl345_read_reg(ADXL345_DATAX0, data, 6);
    if (ret != ESP_OK) return ret;

    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
    return ESP_OK;
}

void calibrate_accel(void)
{
    int32_t sum_x = 0, sum_y = 0;
    int samples = 20;

    printf("Calibrating accelerometer - keep board level...\n");

    for (int i = 0; i < samples; i++) {
        int16_t x, y, z;
        if (adxl345_read(&x, &y, &z) == ESP_OK) {
            sum_x += x;
            sum_y += y;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    cal_x = sum_x / samples;
    cal_y = sum_y / samples;

    printf("Calibration done: x=%d, y=%d\n", cal_x, cal_y);
}
