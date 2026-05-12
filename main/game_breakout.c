#include <stdlib.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static int breakout_paddle_x = 3;        // Paddle center position (0-7)
static int16_t breakout_last_dx = 0;     // For hysteresis
static float breakout_ball_x = 3.5f;     // Ball position (float for smooth movement)
static float breakout_ball_y = 5.5f;
static float breakout_ball_vx = 0.3f;    // Ball velocity
static float breakout_ball_vy = -0.3f;
static uint8_t breakout_bricks[3];       // 3 rows of 8 bricks (bitmask)
static int breakout_bricks_left = 24;
static bool breakout_won = false;
static bool breakout_lost = false;
static int breakout_anim_counter = 0;

void breakout_init(void)
{
    breakout_paddle_x = 3;
    breakout_last_dx = 0;
    breakout_ball_x = 3.5f;
    breakout_ball_y = 5.5f;
    breakout_ball_vx = 0.15f + (esp_random() % 10) * 0.01f;
    breakout_ball_vy = -0.2f;
    breakout_won = false;
    breakout_lost = false;
    breakout_anim_counter = 0;

    // Initialize all bricks (3 rows of 8)
    breakout_bricks[0] = 0xFF;
    breakout_bricks[1] = 0xFF;
    breakout_bricks[2] = 0xFF;
    breakout_bricks_left = 24;
}

void breakout_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dy; (void)z;

    // Handle win/lose animations
    if (breakout_won || breakout_lost) {
        breakout_anim_counter++;
        if (breakout_anim_counter > 60) {
            breakout_init();
        }
        return;
    }

    // Move paddle based on tilt angle with hysteresis to reduce flicker
    // Apply low-pass filter to tilt input
    breakout_last_dx = (breakout_last_dx * 3 + dx) / 4;

    // Map filtered tilt to paddle position with hysteresis
    // Only change position if we've moved enough past the threshold
    int target_pos = 3 + (breakout_last_dx / 25);
    if (target_pos < 1) target_pos = 1;
    if (target_pos > 6) target_pos = 6;

    // Hysteresis: require extra movement to change position
    int current_center = (breakout_paddle_x - 3) * 25;
    int hysteresis = 8;  // Dead zone around current position
    if (breakout_last_dx > current_center + hysteresis ||
        breakout_last_dx < current_center - hysteresis) {
        breakout_paddle_x = target_pos;
    }

    // Move ball
    breakout_ball_x += breakout_ball_vx;
    breakout_ball_y += breakout_ball_vy;

    // Ball collision with walls
    if (breakout_ball_x <= 0.0f) {
        breakout_ball_x = 0.0f;
        breakout_ball_vx = -breakout_ball_vx;
    }
    if (breakout_ball_x >= 7.0f) {
        breakout_ball_x = 7.0f;
        breakout_ball_vx = -breakout_ball_vx;
    }
    if (breakout_ball_y <= 0.0f) {
        breakout_ball_y = 0.0f;
        breakout_ball_vy = -breakout_ball_vy;
    }

    // Ball fell off bottom - lose
    if (breakout_ball_y >= 7.5f) {
        breakout_lost = true;
        breakout_anim_counter = 0;
        return;
    }

    // Ball collision with paddle (paddle is at row 7, spans 3 pixels)
    if (breakout_ball_vy > 0 && breakout_ball_y >= 6.0f && breakout_ball_y <= 6.8f) {
        int ball_ix = (int)breakout_ball_x;
        if (ball_ix >= breakout_paddle_x - 1 && ball_ix <= breakout_paddle_x + 1) {
            breakout_ball_vy = -breakout_ball_vy;
            breakout_ball_y = 6.0f;
            // Add some angle based on where ball hit paddle
            float hit_offset = breakout_ball_x - breakout_paddle_x;
            breakout_ball_vx += hit_offset * 0.05f;
            // Clamp velocity
            if (breakout_ball_vx > 0.25f) breakout_ball_vx = 0.25f;
            if (breakout_ball_vx < -0.25f) breakout_ball_vx = -0.25f;
        }
    }

    // Ball collision with bricks (rows 0, 1, 2)
    int ball_ix = (int)breakout_ball_x;
    int ball_iy = (int)breakout_ball_y;

    if (ball_iy >= 0 && ball_iy <= 2 && ball_ix >= 0 && ball_ix <= 7) {
        if (breakout_bricks[ball_iy] & (1 << ball_ix)) {
            // Hit a brick - destroy it
            breakout_bricks[ball_iy] &= ~(1 << ball_ix);
            breakout_bricks_left--;
            breakout_ball_vy = -breakout_ball_vy;

            // Check win
            if (breakout_bricks_left == 0) {
                breakout_won = true;
                breakout_anim_counter = 0;
            }
        }
    }
}

void breakout_draw(void)
{
    clear_display();

    // Win animation - rainbow explosion
    if (breakout_won) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int dist = abs(x - 3) + abs(y - 3);
                int wave = (breakout_anim_counter * 2 - dist * 3);
                if (wave > 0 && wave < 20) {
                    uint8_t hue = (x * 30 + y * 30 + breakout_anim_counter * 8) & 0xFF;
                    uint8_t brightness = (20 - wave) * 4;
                    uint8_t g, r, b;
                    hsv_to_rgb(hue, 255, brightness, &g, &r, &b);
                    set_pixel_at(y, x, g, r, b);
                }
            }
        }
        return;
    }

    // Lose animation - red flash
    if (breakout_lost) {
        if ((breakout_anim_counter / 5) % 2 == 0) {
            for (int x = breakout_paddle_x - 1; x <= breakout_paddle_x + 1; x++) {
                if (x >= 0 && x <= 7) {
                    set_pixel_at(7, x, 0x00, 0x40, 0x00);
                }
            }
        }
        return;
    }

    // Draw bricks with colors per row
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 8; col++) {
            if (breakout_bricks[row] & (1 << col)) {
                switch (row) {
                    case 0: set_pixel_at(row, col, 0x00, 0x40, 0x00); break;  // Red
                    case 1: set_pixel_at(row, col, 0x30, 0x30, 0x00); break;  // Yellow
                    case 2: set_pixel_at(row, col, 0x40, 0x00, 0x00); break;  // Green
                }
            }
        }
    }

    // Draw paddle (cyan, 3 pixels wide)
    for (int x = breakout_paddle_x - 1; x <= breakout_paddle_x + 1; x++) {
        if (x >= 0 && x <= 7) {
            set_pixel_at(7, x, 0x30, 0x00, 0x30);
        }
    }

    // Draw ball (white)
    int bx = (int)breakout_ball_x;
    int by = (int)breakout_ball_y;
    if (bx >= 0 && bx <= 7 && by >= 0 && by <= 7) {
        set_pixel_at(by, bx, 0xFF, 0xFF, 0xFF);
    }
}
