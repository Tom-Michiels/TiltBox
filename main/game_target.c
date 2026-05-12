#include <stdint.h>
#include <stdbool.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static float target_cursor_x = 3.5f;   // Cursor follows tilt (float for smooth)
static float target_cursor_y = 3.5f;
static int target_pos_x = 0;           // Current target position
static int target_pos_y = 0;
static int target_score = 0;
static int target_timer = 600;         // ~30 seconds at 50ms/frame
static bool target_active = true;
static int target_flash_counter = 0;   // Flash on hit
static int target_end_counter = 0;     // Display final score

void target_init(void)
{
    target_cursor_x = 3.5f;
    target_cursor_y = 3.5f;
    target_score = 0;
    target_timer = 600;
    target_active = true;
    target_flash_counter = 0;
    target_end_counter = 0;

    // Spawn first target at random position
    target_pos_x = esp_random() % 8;
    target_pos_y = esp_random() % 8;
}

void target_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    if (!target_active) {
        target_end_counter++;
        if (target_end_counter > 80) {
            target_init();
        }
        return;
    }

    // Brief flash on hit, don't move cursor during flash
    if (target_flash_counter > 0) {
        target_flash_counter--;
        return;
    }

    // Move cursor with tilt (smooth like glow_ball)
    target_cursor_x += dx / 120.0f;
    target_cursor_y += dy / 120.0f;

    // Clamp to display bounds
    if (target_cursor_x < 0.0f) target_cursor_x = 0.0f;
    if (target_cursor_x > 7.0f) target_cursor_x = 7.0f;
    if (target_cursor_y < 0.0f) target_cursor_y = 0.0f;
    if (target_cursor_y > 7.0f) target_cursor_y = 7.0f;

    // Check if cursor overlaps target (both rounded to int match)
    int cx = (int)(target_cursor_x + 0.5f);
    int cy = (int)(target_cursor_y + 0.5f);
    if (cx == target_pos_x && cy == target_pos_y) {
        target_score++;
        target_flash_counter = 5;

        // Spawn new target at random position (not same as current)
        int new_x, new_y;
        do {
            new_x = esp_random() % 8;
            new_y = esp_random() % 8;
        } while (new_x == target_pos_x && new_y == target_pos_y);
        target_pos_x = new_x;
        target_pos_y = new_y;
    }

    // Timer countdown
    target_timer--;
    if (target_timer <= 0) {
        target_active = false;
        target_end_counter = 0;
    }
}

void target_draw(void)
{
    clear_display();

    if (!target_active) {
        // Show final score as lit pixels in center area, rainbow colors
        int count = target_score;
        if (count > 16) count = 16;
        for (int i = 0; i < count; i++) {
            int row = 2 + (i / 4);
            int col = 2 + (i % 4);
            uint8_t g, r, b;
            hsv_to_rgb((i * 32) & 0xFF, 255, 80, &g, &r, &b);
            set_pixel_at(row, col, g, r, b);
        }
        return;
    }

    // Draw target as bright colored pixel, cycling hue
    uint8_t g, r, b;
    hsv_to_rgb((anim_frame * 8) & 0xFF, 255, 80, &g, &r, &b);
    set_pixel_at(target_pos_y, target_pos_x, g, r, b);

    // Draw cursor as dim white pixel at rounded position
    int cx = (int)(target_cursor_x + 0.5f);
    int cy = (int)(target_cursor_y + 0.5f);
    if (cx >= 0 && cx < 8 && cy >= 0 && cy < 8) {
        set_pixel_at(cy, cx, 0x40, 0x40, 0x40);
    }

    // Flash on hit: fill entire display with brief white flash
    if (target_flash_counter > 0) {
        uint8_t brightness = target_flash_counter * 10;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                set_pixel_at(row, col, brightness, brightness, brightness);
            }
        }
    }

    // Draw timer bar along bottom row
    int lit_pixels = target_timer / 75;
    if (lit_pixels > 8) lit_pixels = 8;
    if (lit_pixels < 0) lit_pixels = 0;

    for (int i = 0; i < lit_pixels; i++) {
        // Color transitions: green -> yellow -> red as time runs out
        uint8_t tg, tr, tb;
        float ratio = (float)i / 8.0f;
        if (ratio < 0.5f) {
            // Red (low pixels = time running out)
            tg = 0x00;
            tr = 0x30;
            tb = 0x00;
        } else {
            // Green (high pixels = still plenty of time)
            tg = 0x30;
            tr = 0x00;
            tb = 0x00;
        }
        set_pixel_at(7, i, tg, tr, tb);
    }
}
