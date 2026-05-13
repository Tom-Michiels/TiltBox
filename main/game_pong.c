#include <stdlib.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static int pong_player_x = 3;       // Player paddle center (row 7), 3px wide
static int pong_ai_x = 4;           // AI paddle center (row 0), 3px wide
static float pong_ball_x = 3.5f;    // Ball position
static float pong_ball_y = 3.5f;
static float pong_ball_vx = 0.2f;   // Ball velocity
static float pong_ball_vy = -0.3f;
static int pong_score = 0;          // Volley count
static int pong_misses = 0;         // Misses (game over at 3)
static bool pong_active = true;
static int pong_reset_counter = 0;
static int16_t pong_last_dx = 0;    // Hysteresis filter
static int pong_ai_delay = 0;       // AI reaction delay counter

void pong_init(void)
{
    pong_player_x = 3;
    pong_ai_x = 4;
    pong_ball_x = 3.5f;
    pong_ball_y = 3.5f;
    pong_ball_vx = 0.15f + (esp_random() % 10) * 0.02f;
    if (esp_random() % 2 == 0) pong_ball_vx = -pong_ball_vx;
    pong_ball_vy = -0.3f;
    pong_score = 0;
    pong_misses = 0;
    pong_active = true;
    pong_reset_counter = 0;
    pong_last_dx = 0;
    pong_ai_delay = 0;
}

void pong_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dy; (void)z;

    // Handle inactive state (ball missed)
    if (!pong_active) {
        pong_reset_counter++;
        if (pong_reset_counter >= 60) {
            // Reset ball position but keep score and misses
            pong_ball_x = 3.5f;
            pong_ball_y = 3.5f;
            pong_ball_vx = 0.15f + (esp_random() % 10) * 0.02f;
            if (esp_random() % 2 == 0) pong_ball_vx = -pong_ball_vx;
            pong_ball_vy = -0.3f;
            pong_active = true;
            pong_reset_counter = 0;
        }
        return;
    }

    // Game over state: flash score then full reset
    if (pong_misses >= 3) {
        pong_reset_counter++;
        if (pong_reset_counter >= 60) {
            pong_init();
        }
        return;
    }

    // Apply low-pass filter to tilt input for player paddle
    pong_last_dx = (pong_last_dx * 3 + dx) / 4;

    // Map filtered tilt to paddle position with hysteresis
    int target_pos = 3 + (pong_last_dx / TILT_THRESHOLD);
    if (target_pos < 1) target_pos = 1;
    if (target_pos > 6) target_pos = 6;

    // Hysteresis: require extra movement to change position
    int current_center = (pong_player_x - 3) * TILT_THRESHOLD;
    int hysteresis = 8;
    if (pong_last_dx > current_center + hysteresis ||
        pong_last_dx < current_center - hysteresis) {
        pong_player_x = target_pos;
    }

    // AI paddle movement (row 0): move toward ball every 3 frames
    pong_ai_delay++;
    if (pong_ai_delay >= 3) {
        pong_ai_delay = 0;
        int ball_ix = (int)pong_ball_x;
        if (pong_ai_x < ball_ix && pong_ai_x < 6) {
            pong_ai_x++;
        } else if (pong_ai_x > ball_ix && pong_ai_x > 1) {
            pong_ai_x--;
        }
    }

    // Move ball
    pong_ball_x += pong_ball_vx;
    pong_ball_y += pong_ball_vy;

    // Wall bounces: left and right
    if (pong_ball_x <= 0.0f) {
        pong_ball_x = 0.0f;
        pong_ball_vx = -pong_ball_vx;
    }
    if (pong_ball_x >= 7.0f) {
        pong_ball_x = 7.0f;
        pong_ball_vx = -pong_ball_vx;
    }

    // Player paddle hit (row 7)
    if (pong_ball_vy > 0 && pong_ball_y >= 6.0f && pong_ball_y <= 6.8f) {
        int ball_ix = (int)pong_ball_x;
        if (ball_ix >= pong_player_x - 1 && ball_ix <= pong_player_x + 1) {
            pong_ball_vy = -pong_ball_vy;
            pong_ball_y = 6.0f;
            // Adjust vx based on hit offset
            float hit_offset = pong_ball_x - pong_player_x;
            pong_ball_vx += hit_offset * 0.05f;
            // Clamp velocity
            if (pong_ball_vx > 0.3f) pong_ball_vx = 0.3f;
            if (pong_ball_vx < -0.3f) pong_ball_vx = -0.3f;
            pong_score++;
        }
    }

    // AI paddle hit (row 0)
    if (pong_ball_vy < 0 && pong_ball_y <= 1.0f && pong_ball_y >= 0.2f) {
        int ball_ix = (int)pong_ball_x;
        if (ball_ix >= pong_ai_x - 1 && ball_ix <= pong_ai_x + 1) {
            pong_ball_vy = -pong_ball_vy;
            pong_ball_y = 1.0f;
        }
    }

    // Ball missed past player (bottom)
    if (pong_ball_y > 7.5f) {
        pong_active = false;
        pong_misses++;
        pong_reset_counter = 0;
        // Full reset if 3 misses
        if (pong_misses >= 3) {
            pong_reset_counter = 0;
        }
    }

    // Ball missed past AI (top) - just reflect, player scores
    if (pong_ball_y < -0.5f) {
        pong_ball_vy = -pong_ball_vy;
        pong_ball_y = 0.5f;
        pong_score++;
    }
}

void pong_draw(void)
{
    clear_display();

    // Game over: flash score as lit pixels in top row
    if (pong_misses >= 3) {
        if ((pong_reset_counter / 5) % 2 == 0) {
            int dots = pong_score % 8;
            for (int i = 0; i < dots; i++) {
                set_pixel_at(0, i, 0x10, 0x10, 0x00);  // Dim yellow
            }
        }
        return;
    }

    // Draw AI paddle at row 0: 3 pixels, cyan (0x30, 0x00, 0x30)
    for (int x = pong_ai_x - 1; x <= pong_ai_x + 1; x++) {
        if (x >= 0 && x <= 7) {
            set_pixel_at(0, x, 0x30, 0x00, 0x30);
        }
    }

    // Draw score as dim yellow dots along row 0 in spaces not covered by AI paddle
    int dots = pong_score % 8;
    for (int i = 0; i < dots; i++) {
        // Skip pixels covered by AI paddle
        if (i >= pong_ai_x - 1 && i <= pong_ai_x + 1) continue;
        set_pixel_at(0, i, 0x10, 0x10, 0x00);  // Dim yellow
    }

    // Draw player paddle at row 7: 3 pixels, green (0x30, 0x10, 0x00)
    for (int x = pong_player_x - 1; x <= pong_player_x + 1; x++) {
        if (x >= 0 && x <= 7) {
            set_pixel_at(7, x, 0x30, 0x10, 0x00);
        }
    }

    // Draw ball as white pixel
    if (pong_active) {
        int bx = (int)pong_ball_x;
        int by = (int)pong_ball_y;
        if (bx >= 0 && bx <= 7 && by >= 0 && by <= 7) {
            set_pixel_at(by, bx, 0xFF, 0xFF, 0xFF);
        }
    }
}
