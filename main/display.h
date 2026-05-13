#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define LED_COUNT 64

extern uint8_t led_data[LED_COUNT * 3];

int get_led_index(int row, int col);
void set_pixel_at(int row, int col, uint8_t g, uint8_t r, uint8_t b);
void clear_display(void);
void ws2812_init(void);
void ws2812_send(void);
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *g, uint8_t *r, uint8_t *b);

#endif // DISPLAY_H
