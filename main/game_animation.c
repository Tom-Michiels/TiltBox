#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "game_common.h"
#include "display.h"

typedef enum {
    ANIM_SPIRAL,
    ANIM_WAVE,
    ANIM_RAINBOW,
    ANIM_COUNT
} animation_t;

static animation_t current_anim = ANIM_SPIRAL;
static uint32_t anim_cycle_counter = 0;
#define ANIM_CYCLE_FRAMES 200

static void draw_spiral(void)
{
    // Spiral coordinates from center outward
    static const int8_t spiral_x[] = {3,4,4,3,3,4,5,5,5,4,3,2,2,2,2,3,4,5,6,6,6,6,6,5,4,3,2,1,1,1,1,1,1,2,3,4,5,6,7,7,7,7,7,7,7,7,6,5,4,3,2,1,0,0,0,0,0,0,0,0,0,1,2,3};
    static const int8_t spiral_y[] = {3,3,4,4,3,3,3,4,5,5,5,5,4,3,2,2,2,2,2,3,4,5,6,6,6,6,6,6,5,4,3,2,1,1,1,1,1,1,1,2,3,4,5,6,7,7,7,7,7,7,7,7,7,6,5,4,3,2,1,0,0,0,0,0};

    // Moving head position with longer visible trail
    int head = (anim_frame / 2) % 64;
    int trail_len = 24;  // Longer trail

    for (int t = 0; t < trail_len; t++) {
        int i = (head - t + 64) % 64;

        // Fade brightness along trail (head is brightest)
        uint8_t brightness = (trail_len - t) * 3;
        if (brightness > 72) brightness = 72;

        // Rainbow hue shifts along the trail and over time
        uint8_t hue = (i * 4 + anim_frame * 2) & 0xFF;
        uint8_t g, r, b;
        hsv_to_rgb(hue, 255, brightness, &g, &r, &b);

        set_pixel_at(spiral_y[i], spiral_x[i], g, r, b);
    }
}

static void draw_wave(void)
{
    for (int x = 0; x < 8; x++) {
        float wave = 3.5f + 3.0f * sinf((x + anim_frame * 0.15f) * 0.8f);
        int y = (int)wave;
        if (y >= 0 && y < 8) {
            // Blue wave with some gradient
            uint8_t hue = (x * 20 + anim_frame) & 0xFF;
            uint8_t g, r, b;
            hsv_to_rgb(hue, 255, 64, &g, &r, &b);
            set_pixel_at(y, x, g, r, b);
        }
    }
}

static void draw_rainbow(void)
{
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t hue = (x * 20 + y * 20 + anim_frame * 3) & 0xFF;
            uint8_t g, r, b;
            hsv_to_rgb(hue, 255, 48, &g, &r, &b);
            set_pixel_at(y, x, g, r, b);
        }
    }
}

void animation_init(void)
{
    anim_frame = 0;
    anim_cycle_counter = 0;
    current_anim = ANIM_SPIRAL;
}

void animation_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dx; (void)dy; (void)z;

    anim_frame++;
    anim_cycle_counter++;

    if (anim_cycle_counter >= ANIM_CYCLE_FRAMES) {
        anim_cycle_counter = 0;
        current_anim = (current_anim + 1) % ANIM_COUNT;
    }
}

void animation_draw(void)
{
    clear_display();

    switch (current_anim) {
        case ANIM_SPIRAL:
            draw_spiral();
            break;
        case ANIM_WAVE:
            draw_wave();
            break;
        case ANIM_RAINBOW:
            draw_rainbow();
            break;
        default:
            break;
    }
}
