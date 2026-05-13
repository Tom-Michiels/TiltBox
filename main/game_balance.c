#include <math.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static float bal_ball_x = 3.5f;
static float bal_ball_y = 3.5f;
static float bal_wind_x = 0.0f;       // Current wind force
static float bal_wind_y = 0.0f;
static int bal_wind_timer = 0;         // Frames until wind changes
static float bal_target_radius = 3.0f; // Shrinks over time
static bool bal_alive = true;
static int bal_death_counter = 0;
static int bal_survival_frames = 0;

static float bal_last_ball_x = 3.5f;  // For death flash position
static float bal_last_ball_y = 3.5f;

void balance_init(void)
{
    bal_ball_x = 3.5f;
    bal_ball_y = 3.5f;
    bal_wind_x = 0.0f;
    bal_wind_y = 0.0f;
    bal_wind_timer = 0;
    bal_target_radius = 3.0f;
    bal_alive = true;
    bal_death_counter = 0;
    bal_survival_frames = 0;
    bal_last_ball_x = 3.5f;
    bal_last_ball_y = 3.5f;
}

void balance_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    // Handle death: flash then auto-restart
    if (!bal_alive) {
        bal_death_counter++;
        if (bal_death_counter >= 40) {
            balance_init();
        }
        return;
    }

    // Apply tilt force from player input
    bal_ball_x += dx / 300.0f;
    bal_ball_y += dy / 300.0f;

    // Apply wind force
    bal_ball_x += bal_wind_x;
    bal_ball_y += bal_wind_y;

    // Wind timer: change wind direction every 40 frames
    bal_wind_timer++;
    if (bal_wind_timer >= 40) {
        bal_wind_timer = 0;
        // Random wind: range roughly -0.1 to +0.1
        bal_wind_x = (float)((int)(esp_random() % 100) - 50) / 500.0f;
        bal_wind_y = (float)((int)(esp_random() % 100) - 50) / 500.0f;
    }

    // Clamp ball position to display bounds
    if (bal_ball_x < 0.0f) bal_ball_x = 0.0f;
    if (bal_ball_x > 7.0f) bal_ball_x = 7.0f;
    if (bal_ball_y < 0.0f) bal_ball_y = 0.0f;
    if (bal_ball_y > 7.0f) bal_ball_y = 7.0f;

    // Shrink target radius every 100 frames (min 0.5)
    if (bal_survival_frames > 0 && bal_survival_frames % 100 == 0) {
        bal_target_radius -= 0.2f;
        if (bal_target_radius < 0.5f) bal_target_radius = 0.5f;
    }

    // Save last position for death flash
    bal_last_ball_x = bal_ball_x;
    bal_last_ball_y = bal_ball_y;

    bal_survival_frames++;
}

void balance_draw(void)
{
    clear_display();

    // Death flash: red at last ball position
    if (!bal_alive) {
        if ((bal_death_counter / 3) % 2 == 0) {
            int bx = (int)bal_last_ball_x;
            int by = (int)bal_last_ball_y;
            if (bx >= 0 && bx <= 7 && by >= 0 && by <= 7) {
                set_pixel_at(by, bx, 0x00, 0x40, 0x00);  // Red
            }
        }
        return;
    }

    // Draw target zone: dim blue ring at target_radius from center (3.5, 3.5)
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            float dist = sqrtf((col - 3.5f) * (col - 3.5f) + (row - 3.5f) * (row - 3.5f));
            if (dist >= bal_target_radius - 0.5f && dist <= bal_target_radius + 0.5f) {
                set_pixel_at(row, col, 0x00, 0x00, 0x20);  // Dim blue
            }
        }
    }

    // Draw center target dot as dim green
    set_pixel_at(3, 3, 0x10, 0x08, 0x00);
    set_pixel_at(3, 4, 0x10, 0x08, 0x00);
    set_pixel_at(4, 3, 0x10, 0x08, 0x00);
    set_pixel_at(4, 4, 0x10, 0x08, 0x00);

    // Draw ball as bright white pixel
    int bx = (int)bal_ball_x;
    int by = (int)bal_ball_y;
    if (bx >= 0 && bx <= 7 && by >= 0 && by <= 7) {
        set_pixel_at(by, bx, 0xFF, 0xFF, 0xFF);
    }
}
