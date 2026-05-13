#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "game_common.h"
#include "display.h"
#include "accel.h"

// Shared animation frame counter
uint32_t anim_frame = 0;

// Current game state
static game_t current_game = GAME_MAZE;

// Flip detection state
static bool z_positive = true;
static int flip_debounce_counter = 0;

// Forward declarations for all game functions
extern void ball_trail_init(void);
extern void ball_trail_update(int16_t dx, int16_t dy, int16_t z);
extern void ball_trail_draw(void);

extern void glow_ball_init(void);
extern void glow_ball_update(int16_t dx, int16_t dy, int16_t z);
extern void glow_ball_draw(void);

extern void snake_init(void);
extern void snake_update(int16_t dx, int16_t dy, int16_t z);
extern void snake_draw(void);

extern void maze_init(void);
extern void maze_update(int16_t dx, int16_t dy, int16_t z);
extern void maze_draw(void);

extern void breakout_init(void);
extern void breakout_update(int16_t dx, int16_t dy, int16_t z);
extern void breakout_draw(void);

extern void dodge_init(void);
extern void dodge_update(int16_t dx, int16_t dy, int16_t z);
extern void dodge_draw(void);

extern void scroll_text_init(void);
extern void scroll_text_update(int16_t dx, int16_t dy, int16_t z);
extern void scroll_text_draw(void);

extern void animation_init(void);
extern void animation_update(int16_t dx, int16_t dy, int16_t z);
extern void animation_draw(void);

extern void tetris_init(void);
extern void tetris_update(int16_t dx, int16_t dy, int16_t z);
extern void tetris_draw(void);

extern void pong_init(void);
extern void pong_update(int16_t dx, int16_t dy, int16_t z);
extern void pong_draw(void);

extern void balance_init(void);
extern void balance_update(int16_t dx, int16_t dy, int16_t z);
extern void balance_draw(void);

extern void catch_game_init(void);
extern void catch_game_update(int16_t dx, int16_t dy, int16_t z);
extern void catch_game_draw(void);

extern void target_init(void);
extern void target_update(int16_t dx, int16_t dy, int16_t z);
extern void target_draw(void);

extern void sokoban_init(void);
extern void sokoban_update(int16_t dx, int16_t dy, int16_t z);
extern void sokoban_draw(void);

extern void marble_race_init(void);
extern void marble_race_update(int16_t dx, int16_t dy, int16_t z);
extern void marble_race_draw(void);

// Game dispatch table
static const game_interface_t game_table[GAME_COUNT] = {
    { maze_init, maze_update, maze_draw },
    { ball_trail_init, ball_trail_update, ball_trail_draw },
    { glow_ball_init, glow_ball_update, glow_ball_draw },
    { snake_init, snake_update, snake_draw },
    { breakout_init, breakout_update, breakout_draw },
    { dodge_init, dodge_update, dodge_draw },
    { scroll_text_init, scroll_text_update, scroll_text_draw },
    { animation_init, animation_update, animation_draw },
    { tetris_init, tetris_update, tetris_draw },
    { pong_init, pong_update, pong_draw },
    { balance_init, balance_update, balance_draw },
    { catch_game_init, catch_game_update, catch_game_draw },
    { target_init, target_update, target_draw },
    { sokoban_init, sokoban_update, sokoban_draw },
    { marble_race_init, marble_race_update, marble_race_draw },
};

static bool check_game_switch(int16_t z)
{
    bool currently_positive = z > Z_POSITIVE_THRESHOLD;
    bool currently_negative = z < Z_NEGATIVE_THRESHOLD;

    if (z_positive && currently_negative) {
        flip_debounce_counter++;
        if (flip_debounce_counter >= FLIP_DEBOUNCE_COUNT) {
            z_positive = false;
            flip_debounce_counter = 0;
            return true;
        }
    } else if (!z_positive && currently_positive) {
        flip_debounce_counter++;
        if (flip_debounce_counter >= FLIP_DEBOUNCE_COUNT) {
            z_positive = true;
            flip_debounce_counter = 0;
        }
    } else {
        flip_debounce_counter = 0;
    }

    return false;
}

void app_main(void)
{
    printf("Multi-Game LED Matrix starting...\n");

    ws2812_init();
    i2c_init();

    // Show green while initializing
    clear_display();
    set_pixel_at(3, 3, GREEN);
    ws2812_send();

    esp_err_t ret = adxl345_init();
    if (ret != ESP_OK) {
        printf("ADXL345 init failed: %d\n", ret);
        clear_display();
        set_pixel_at(3, 3, RED);
        ws2812_send();
        while (1) vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    printf("ADXL345 initialized\n");
    calibrate_accel();

    // Initialize flip detection state based on current orientation
    {
        int16_t x, y, z;
        if (adxl345_read(&x, &y, &z) == ESP_OK) {
            z_positive = (z > 0);
        }
    }

    // Initialize first game
    game_table[current_game].init();
    printf("Starting game %d\n", current_game);

    while (1) {
        int16_t x, y, z;

        if (adxl345_read(&x, &y, &z) == ESP_OK) {
            int16_t raw_dx = x - cal_x;
            int16_t raw_dy = y - cal_y;

            // Rotate 90 degrees counter-clockwise relative to sensor
            int16_t dx = -raw_dy;
            int16_t dy = raw_dx;

            // Check for game switch (flip detection)
            if (check_game_switch(z)) {
                current_game = (current_game + 1) % GAME_COUNT;
                game_table[current_game].init();
                printf("Switched to game %d\n", current_game);
            }

            // Update current game
            game_table[current_game].update(dx, dy, z);
        }

        // Increment shared animation frame
        anim_frame++;

        // Draw current game
        game_table[current_game].draw();
        ws2812_send();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
