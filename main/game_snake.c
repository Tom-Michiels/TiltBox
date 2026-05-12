#include <stdlib.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

#define MAX_SNAKE_LENGTH 64
#define SNAKE_MOVE_INTERVAL 6
static position_t snake_body[MAX_SNAKE_LENGTH];
static int snake_length = 3;
static int8_t snake_dir_x = 1, snake_dir_y = 0;
static position_t food_pos;
static bool snake_alive = true;
static int snake_move_counter = 0;

static void spawn_food(void)
{
    bool valid;
    do {
        valid = true;
        food_pos.x = esp_random() % 8;
        food_pos.y = esp_random() % 8;
        // Check not on snake
        for (int i = 0; i < snake_length; i++) {
            if (snake_body[i].x == food_pos.x && snake_body[i].y == food_pos.y) {
                valid = false;
                break;
            }
        }
    } while (!valid);
}

void snake_init(void)
{
    snake_length = 3;
    snake_alive = true;
    snake_dir_x = 1;
    snake_dir_y = 0;
    snake_move_counter = 0;

    // Initialize snake in center going right
    for (int i = 0; i < snake_length; i++) {
        snake_body[i].x = 3 - i;
        snake_body[i].y = 3;
    }

    spawn_food();
}

void snake_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    if (!snake_alive) {
        if (++snake_move_counter > 40) {
            snake_init();
        }
        return;
    }

    // Update direction from tilt (prevent 180 turns)
    if (abs(dx) > abs(dy)) {
        if (dx > TILT_THRESHOLD && snake_dir_x != -1) {
            snake_dir_x = 1; snake_dir_y = 0;
        } else if (dx < -TILT_THRESHOLD && snake_dir_x != 1) {
            snake_dir_x = -1; snake_dir_y = 0;
        }
    } else {
        if (dy > TILT_THRESHOLD && snake_dir_y != -1) {
            snake_dir_x = 0; snake_dir_y = 1;
        } else if (dy < -TILT_THRESHOLD && snake_dir_y != 1) {
            snake_dir_x = 0; snake_dir_y = -1;
        }
    }

    // Move snake at interval
    if (++snake_move_counter >= SNAKE_MOVE_INTERVAL) {
        snake_move_counter = 0;

        int8_t new_x = snake_body[0].x + snake_dir_x;
        int8_t new_y = snake_body[0].y + snake_dir_y;

        // Clamp to walls (don't die, just stop)
        if (new_x < 0) new_x = 0;
        if (new_x > 7) new_x = 7;
        if (new_y < 0) new_y = 0;
        if (new_y > 7) new_y = 7;

        // If we can't move (hit wall), just wait
        if (new_x == snake_body[0].x && new_y == snake_body[0].y) {
            return;
        }

        // Check self collision (still dies on self-hit, skip head at i=0)
        for (int i = 1; i < snake_length; i++) {
            if (snake_body[i].x == new_x && snake_body[i].y == new_y) {
                snake_alive = false;
                return;
            }
        }

        // Check food
        bool ate_food = (new_x == food_pos.x && new_y == food_pos.y);

        // Move body
        if (!ate_food) {
            for (int i = snake_length - 1; i > 0; i--) {
                snake_body[i] = snake_body[i-1];
            }
        } else {
            // Grow
            for (int i = snake_length; i > 0; i--) {
                snake_body[i] = snake_body[i-1];
            }
            if (snake_length < MAX_SNAKE_LENGTH) {
                snake_length++;
            }
            spawn_food();
        }

        snake_body[0].x = new_x;
        snake_body[0].y = new_y;
    }
}

void snake_draw(void)
{
    clear_display();

    if (!snake_alive) {
        // Flash red on death
        if ((snake_move_counter / 4) % 2 == 0) {
            for (int i = 0; i < snake_length; i++) {
                set_pixel_at(snake_body[i].y, snake_body[i].x, 0x00, 0x40, 0x00);
            }
        }
        return;
    }

    // Draw snake (green gradient - head brighter)
    set_pixel_at(snake_body[0].y, snake_body[0].x, 0x60, 0x00, 0x00);
    for (int i = 1; i < snake_length; i++) {
        set_pixel_at(snake_body[i].y, snake_body[i].x, 0x30, 0x00, 0x00);
    }

    // Draw food (red, blinking)
    if ((anim_frame / 5) % 2 == 0) {
        set_pixel_at(food_pos.y, food_pos.x, 0x00, 0x40, 0x00);
    }
}
