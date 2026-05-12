#include <stdint.h>
#include <stdbool.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

#define MARBLE_TRACK_LEN 64

static int8_t marble_track[MARBLE_TRACK_LEN];
static int marble_track_pos = 0;
static float marble_x = 3.5f;
static int16_t marble_last_dx = 0;
static int marble_score = 0;
static int marble_lives = 3;
static bool marble_alive = true;
static int marble_anim_counter = 0;
static float marble_scroll_speed = 0.15f;
static float marble_scroll_accum = 0.0f;
static int marble_track_width = 3;

static void marble_generate_track(void)
{
    int8_t center = 3 + (esp_random() % 2); // Start at 3 or 4
    marble_track[0] = center;

    for (int i = 1; i < MARBLE_TRACK_LEN; i++) {
        uint32_t r = esp_random() % 10;
        if (r < 4) {
            // Stay straight
        } else if (r < 7) {
            // Shift left
            if (center > 1) center--;
        } else {
            // Shift right
            if (center < 6) center++;
        }
        marble_track[i] = center;
    }
}

void marble_race_init(void)
{
    marble_generate_track();
    marble_track_pos = 0;
    marble_x = (float)marble_track[0];
    marble_last_dx = 0;
    marble_score = 0;
    marble_lives = 3;
    marble_alive = true;
    marble_anim_counter = 0;
    marble_scroll_speed = 0.15f;
    marble_scroll_accum = 0.0f;
    marble_track_width = 3;
}

void marble_race_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dy; (void)z;

    if (!marble_alive) {
        marble_anim_counter++;
        if (marble_lives <= 0 && marble_anim_counter > 50) {
            // Game over - restart
            marble_race_init();
            return;
        }
        if (marble_lives > 0 && marble_anim_counter > 30) {
            // Respawn: snap marble back to track center
            int idx = (marble_track_pos + 7) % MARBLE_TRACK_LEN;
            marble_x = (float)marble_track[idx];
            marble_alive = true;
            marble_anim_counter = 0;
        }
        return;
    }

    // Hysteresis: low-pass filter on tilt
    marble_last_dx = (marble_last_dx * 3 + dx) / 4;

    // Move marble horizontally
    marble_x += marble_last_dx / 100.0f;
    if (marble_x < 0.0f) marble_x = 0.0f;
    if (marble_x > 7.0f) marble_x = 7.0f;

    // Scroll track
    marble_scroll_accum += marble_scroll_speed;
    if (marble_scroll_accum >= 1.0f) {
        marble_scroll_accum -= 1.0f;
        marble_track_pos++;
        marble_score++;

        // Speed up every 20 points
        if (marble_score % 20 == 0) {
            marble_scroll_speed += 0.02f;
            if (marble_scroll_speed > 0.4f) marble_scroll_speed = 0.4f;
        }

        // Regenerate track if wrapped
        if (marble_track_pos >= MARBLE_TRACK_LEN) {
            marble_track_pos = 0;
            marble_generate_track();
        }

        // Check if marble is on the track
        int idx = (marble_track_pos + 7) % MARBLE_TRACK_LEN;
        int8_t track_center = marble_track[idx];
        float offset = marble_x - (float)track_center;
        if (offset < 0) offset = -offset;

        if (offset > (float)marble_track_width + 0.5f) {
            // Off track!
            marble_lives--;
            marble_alive = false;
            marble_anim_counter = 0;
        }
    }
}

void marble_race_draw(void)
{
    clear_display();

    if (!marble_alive && marble_lives <= 0) {
        // Game over: show score as lit pixels
        for (int i = 0; i < 8 && i < marble_score; i++) {
            set_pixel_at(3, i, 0x00, 0x40, 0x00); // Red dots for score
        }
        // Also show remaining as dimmer dots if score > 8
        if (marble_score > 8) {
            for (int i = 0; i < 8 && (i + 8) < marble_score; i++) {
                set_pixel_at(4, i, 0x00, 0x20, 0x00);
            }
        }
        return;
    }

    // Draw track
    for (int row = 0; row < 8; row++) {
        int idx = (marble_track_pos + row) % MARBLE_TRACK_LEN;
        int8_t center = marble_track[idx];
        int left = center - marble_track_width;
        int right = center + marble_track_width;
        if (left < 0) left = 0;
        if (right > 7) right = 7;

        for (int col = left; col <= right; col++) {
            // Track edge dimmer, center brighter
            int dist_from_center = col - center;
            if (dist_from_center < 0) dist_from_center = -dist_from_center;
            uint8_t brightness = 0x30 - (dist_from_center * 0x08);
            set_pixel_at(row, col, brightness, 0x00, 0x00);
        }
    }

    // Draw marble at bottom row (row 7) as white
    int marble_col = (int)(marble_x + 0.5f);
    if (marble_col < 0) marble_col = 0;
    if (marble_col > 7) marble_col = 7;
    set_pixel_at(7, marble_col, 0x40, 0x40, 0x40);

    // Draw lives as dim green dots in top-left
    for (int i = 0; i < marble_lives && i < 3; i++) {
        set_pixel_at(0, i, 0x10, 0x00, 0x00);
    }

    // Draw score indicator (dim dots at top-right, one dot per 10 points)
    int score_tens = marble_score / 10;
    for (int i = 0; i < score_tens && i < 4; i++) {
        set_pixel_at(0, 7 - i, 0x08, 0x08, 0x00);
    }
}
