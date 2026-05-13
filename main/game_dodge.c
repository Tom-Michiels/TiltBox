#include <stdint.h>
#include <stdbool.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static int dodge_player_x = 3;
static int16_t dodge_last_dx = 0;
static float dodge_obstacles[8];
static int dodge_score = 0;
static bool dodge_alive = true;
static int dodge_anim_counter = 0;
static int dodge_speed_counter = 0;
static float dodge_fall_speed = 0.15f;

void dodge_init(void)
{
    dodge_player_x = 3;
    dodge_last_dx = 0;
    dodge_score = 0;
    dodge_alive = true;
    dodge_anim_counter = 0;
    dodge_speed_counter = 0;
    dodge_fall_speed = 0.15f;

    // Clear all obstacles
    for (int i = 0; i < 8; i++) {
        dodge_obstacles[i] = -1.0f;
    }
}

void dodge_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dy; (void)z;

    if (!dodge_alive) {
        dodge_anim_counter++;
        if (dodge_anim_counter > 60) {
            dodge_init();
        }
        return;
    }

    // Move player based on tilt with hysteresis to reduce flicker
    // Apply low-pass filter to tilt input
    dodge_last_dx = (dodge_last_dx * 3 + dx) / 4;

    // Map filtered tilt to position with hysteresis
    int target_x = 3 + (dodge_last_dx / 20);
    if (target_x < 0) target_x = 0;
    if (target_x > 7) target_x = 7;

    // Hysteresis: require extra movement to change position
    int current_center = (dodge_player_x - 3) * 20;
    int hysteresis = 6;  // Dead zone around current position
    if (dodge_last_dx > current_center + hysteresis ||
        dodge_last_dx < current_center - hysteresis) {
        dodge_player_x = target_x;
    }

    // Update obstacles
    for (int i = 0; i < 8; i++) {
        if (dodge_obstacles[i] >= 0) {
            dodge_obstacles[i] += dodge_fall_speed;

            // Check collision with player
            if (dodge_obstacles[i] >= 6.5f && dodge_obstacles[i] <= 7.5f && i == dodge_player_x) {
                dodge_alive = false;
                dodge_anim_counter = 0;
                return;
            }

            // Remove if off screen
            if (dodge_obstacles[i] > 8.0f) {
                dodge_obstacles[i] = -1.0f;
                dodge_score++;
            }
        }
    }

    // Spawn new obstacles randomly
    dodge_speed_counter++;
    int spawn_rate = 15 - (dodge_score / 5);  // Gets faster as score increases
    if (spawn_rate < 5) spawn_rate = 5;

    if (dodge_speed_counter >= spawn_rate) {
        dodge_speed_counter = 0;

        // Find empty column and spawn
        int col = esp_random() % 8;
        if (dodge_obstacles[col] < 0) {
            dodge_obstacles[col] = 0.0f;
        }

        // Increase speed over time
        if (dodge_score > 0 && dodge_score % 10 == 0) {
            dodge_fall_speed += 0.01f;
            if (dodge_fall_speed > 0.4f) dodge_fall_speed = 0.4f;
        }
    }
}

void dodge_draw(void)
{
    clear_display();

    if (!dodge_alive) {
        // Death animation - red flash with score display
        if ((dodge_anim_counter / 5) % 2 == 0) {
            // Flash player position red
            set_pixel_at(7, dodge_player_x, 0x00, 0x50, 0x00);

            // Show score as lit pixels in top row
            for (int i = 0; i < 8 && i < dodge_score; i++) {
                set_pixel_at(0, i, 0x40, 0x40, 0x00);
            }
        }
        return;
    }

    // Draw obstacles (red/orange, brighter as they fall)
    for (int i = 0; i < 8; i++) {
        if (dodge_obstacles[i] >= 0) {
            int y = (int)dodge_obstacles[i];
            if (y >= 0 && y < 8) {
                uint8_t brightness = 0x20 + (y * 0x08);
                set_pixel_at(y, i, 0x00, brightness, 0x00);
            }
        }
    }

    // Draw player (cyan)
    set_pixel_at(7, dodge_player_x, 0x40, 0x00, 0x40);

    // Draw score indicator (dim yellow dots at top)
    for (int i = 0; i < (dodge_score % 8); i++) {
        set_pixel_at(0, i, 0x10, 0x10, 0x00);
    }
}
