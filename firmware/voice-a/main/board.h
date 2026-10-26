#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

enum { TB_WAKE=4, TB_IMU_INT=5, TB_SDA=6, TB_SCL=7, TB_BAT_ADC=8,
       TB_LED_FAULT=9, TB_BOOST_EN=10, TB_LED_EN=11, TB_LED_DATA=12,
       TB_MIC_WS=13, TB_MIC_BCLK=14, TB_MIC_SD=15, TB_MIC_EN=16,
       TB_CHG_STAT1=17, TB_CHG_STAT2=18, TB_VBUS=21 };
enum { CMD_NONE=0, CMD_NEXT, CMD_PAUSE, CMD_PLAY, CMD_SLEEP, CMD_MAZE,
       CMD_SNAKE, CMD_TETRIS, CMD_BRIGHTER, CMD_DIMMER, CMD_CALIBRATE };
void tb_init(void);
void tb_led_send(const uint8_t *grb);
esp_err_t tb_imu_init(void);
esp_err_t tb_imu_read(int16_t *x,int16_t *y,int16_t *z);
esp_err_t tb_gyro_read(int16_t *x,int16_t *y,int16_t *z); /* deg/s, screen axes */
bool tb_face_down(void);      /* LEDs pointing down (z < -150) */
bool tb_button_pressed(void); /* edge button, active low */
int tb_poll_command(void);
void tb_set_brightness(int delta);
void tb_sleep(bool low_battery);
int tb_battery_mv(void);
void tb_voice_start(void);
void tb_voice_stop(void);
void tb_voice_poll(void);
void tb_audio_stop(void);
void tb_led_power(bool on);
void tb_queue_command(int command);
i2s_chan_handle_t tb_audio_channel(void);
