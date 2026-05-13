#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "esp_system.h"
#include "display.h"

#define WS2812_PIN      GPIO_NUM_10
#define RMT_CHANNEL     RMT_CHANNEL_0

// WS2812 timing (in RMT ticks at 10MHz = 100ns per tick)
#define T0H  4
#define T0L  8
#define T1H  8
#define T1L  4

static rmt_item32_t rmt_items[LED_COUNT * 24];

uint8_t led_data[LED_COUNT * 3];

int get_led_index(int row, int col)
{
    return row * 8 + col;
}

void set_pixel_at(int row, int col, uint8_t g, uint8_t r, uint8_t b)
{
    if (row >= 0 && row < 8 && col >= 0 && col < 8) {
        int index = get_led_index(row, col);
        led_data[index * 3 + 0] = g;
        led_data[index * 3 + 1] = r;
        led_data[index * 3 + 2] = b;
    }
}

void clear_display(void)
{
    memset(led_data, 0, LED_COUNT * 3);
}

void ws2812_init(void)
{
    rmt_config_t config = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_CHANNEL,
        .gpio_num = WS2812_PIN,
        .clk_div = 8,
        .mem_block_num = 4,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_output_en = true,
            .idle_level = RMT_IDLE_LEVEL_LOW,
        }
    };
    rmt_config(&config);
    rmt_driver_install(RMT_CHANNEL, 0, 0);
}

void ws2812_send(void)
{
    int item_idx = 0;
    for (int i = 0; i < LED_COUNT * 3; i++) {
        uint8_t byte = led_data[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                rmt_items[item_idx].duration0 = T1H;
                rmt_items[item_idx].level0 = 1;
                rmt_items[item_idx].duration1 = T1L;
                rmt_items[item_idx].level1 = 0;
            } else {
                rmt_items[item_idx].duration0 = T0H;
                rmt_items[item_idx].level0 = 1;
                rmt_items[item_idx].duration1 = T0L;
                rmt_items[item_idx].level1 = 0;
            }
            item_idx++;
        }
    }
    rmt_write_items(RMT_CHANNEL, rmt_items, item_idx, true);
}

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *g, uint8_t *r, uint8_t *b)
{
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}
