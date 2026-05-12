#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "driver/i2c.h"
#include "esp_system.h"
#include "esp_random.h"

// I2C config
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         GPIO_NUM_8
#define I2C_SCL         GPIO_NUM_9
#define I2C_FREQ_HZ     100000

// ADXL345 registers
#define ADXL345_ADDR        0x53
#define ADXL345_DEVID       0x00
#define ADXL345_BW_RATE     0x2C
#define ADXL345_POWER_CTL   0x2D
#define ADXL345_DATA_FORMAT 0x31
#define ADXL345_DATAX0      0x32

// WS2812 config
#define WS2812_PIN      GPIO_NUM_10
#define RMT_CHANNEL     RMT_CHANNEL_0
#define LED_COUNT       64

// WS2812 timing (in RMT ticks at 10MHz = 100ns per tick)
#define T0H  4
#define T0L  8
#define T1H  8
#define T1L  4

// Colors (GRB format)
#define BLACK  0x00, 0x00, 0x00
#define WHITE  0xFF, 0xFF, 0xFF
#define RED    0x00, 0x40, 0x00
#define GREEN  0x40, 0x00, 0x00
#define BLUE   0x00, 0x00, 0x40
#define YELLOW 0x40, 0x40, 0x00

// Tilt threshold
#define TILT_THRESHOLD 25

// Flip detection
#define Z_POSITIVE_THRESHOLD  50
#define Z_NEGATIVE_THRESHOLD -50
#define FLIP_DEBOUNCE_COUNT   5

// Game types
typedef enum {
    GAME_MAZE,
    GAME_BALL_TRAIL,
    GAME_GLOW_BALL,
    GAME_SNAKE,
    GAME_BREAKOUT,
    GAME_DODGE,
    GAME_SCROLL_TEXT,
    GAME_ANIMATION,
    GAME_TETRIS,
    GAME_COUNT
} game_t;

typedef struct {
    void (*init)(void);
    void (*update)(int16_t dx, int16_t dy, int16_t z);
    void (*draw)(void);
} game_interface_t;

typedef struct {
    int8_t x, y;
} position_t;

// Display buffers
static uint8_t led_data[LED_COUNT * 3];
static rmt_item32_t rmt_items[LED_COUNT * 24];

// Calibration values
static int16_t cal_x = 0, cal_y = 0;

// Current game state
static game_t current_game = GAME_MAZE;

// Flip detection state
static bool z_positive = true;
static int flip_debounce_counter = 0;

// Animation frame counter (shared)
static uint32_t anim_frame = 0;

// ============ Ball with Trail Game ============
#define TRAIL_LENGTH 8
static position_t trail_buffer[TRAIL_LENGTH];
static int trail_head = 0;
static int ball_x = 3;
static int ball_y = 3;

// ============ Glow Ball Game ============
static float glow_ball_x = 3.5f;
static float glow_ball_y = 3.5f;

// ============ Snake Game ============
#define MAX_SNAKE_LENGTH 64
#define SNAKE_MOVE_INTERVAL 6
static position_t snake_body[MAX_SNAKE_LENGTH];
static int snake_length = 3;
static int8_t snake_dir_x = 1, snake_dir_y = 0;
static position_t food_pos;
static bool snake_alive = true;
static int snake_move_counter = 0;

// ============ Maze Game ============
static uint8_t maze[8];
static position_t maze_ball;
static position_t maze_goal;
static bool maze_won = false;
static int maze_win_counter = 0;

// ============ Dodge Game ============
static int dodge_player_x = 3;
static int16_t dodge_last_dx = 0;  // For hysteresis
static float dodge_obstacles[8];  // Y position of obstacle in each column (-1 = none)
static int dodge_score = 0;
static bool dodge_alive = true;
static int dodge_anim_counter = 0;
static int dodge_speed_counter = 0;
static float dodge_fall_speed = 0.15f;

// ============ Scroll Text Game ============
static float scroll_pos = 8.0f;  // Start off-screen to the right

// ============ Breakout Game ============
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

// ============ Animation Mode ============
typedef enum {
    ANIM_SPIRAL,
    ANIM_WAVE,
    ANIM_RAINBOW,
    ANIM_COUNT
} animation_t;
static animation_t current_anim = ANIM_SPIRAL;
static uint32_t anim_cycle_counter = 0;
#define ANIM_CYCLE_FRAMES 200

// ============ Tetris Game ============
#define TETRIS_DROP_INTERVAL 12
#define TETRIS_MOVE_DELAY 3
static uint8_t tetris_board[8];  // 8 rows, each byte is 8 columns (bitmask)
static int8_t tetris_piece_x, tetris_piece_y;  // Current piece position
static int8_t tetris_piece_type;  // 0-6 for I,O,T,S,Z,L,J
static int8_t tetris_piece_rot;   // 0-3 rotation
static int tetris_drop_counter;
static int tetris_move_counter;
static bool tetris_game_over;
static int tetris_anim_counter;
static int16_t tetris_last_dx;

// Tetromino shapes: 4 rotations each, stored as 4x4 bitmask (16 bits)
// Encoded as which cells are filled relative to piece origin
static const uint16_t tetris_pieces[7][4] = {
    // I piece
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    // O piece
    {0x6600, 0x6600, 0x6600, 0x6600},
    // T piece
    {0x0E40, 0x4C40, 0x4E00, 0x4640},
    // S piece
    {0x06C0, 0x8C40, 0x6C00, 0x4620},
    // Z piece
    {0x0C60, 0x4C80, 0xC600, 0x2640},
    // L piece
    {0x0E80, 0xC440, 0x2E00, 0x44C0},
    // J piece
    {0x0E20, 0x44C0, 0x8E00, 0xC440},
};

// Piece colors (GRB format)
static const uint8_t tetris_colors[7][3] = {
    {0x40, 0x00, 0x40},  // I - cyan
    {0x40, 0x40, 0x00},  // O - yellow
    {0x20, 0x00, 0x40},  // T - purple
    {0x40, 0x00, 0x00},  // S - green
    {0x00, 0x40, 0x00},  // Z - red
    {0x20, 0x40, 0x00},  // L - orange
    {0x00, 0x00, 0x40},  // J - blue
};

// Forward declarations
static void ball_trail_init(void);
static void ball_trail_update(int16_t dx, int16_t dy, int16_t z);
static void ball_trail_draw(void);

static void glow_ball_init(void);
static void glow_ball_update(int16_t dx, int16_t dy, int16_t z);
static void glow_ball_draw(void);

static void snake_init(void);
static void snake_update(int16_t dx, int16_t dy, int16_t z);
static void snake_draw(void);

static void maze_init(void);
static void maze_update(int16_t dx, int16_t dy, int16_t z);
static void maze_draw(void);

static void breakout_init(void);
static void breakout_update(int16_t dx, int16_t dy, int16_t z);
static void breakout_draw(void);

static void dodge_init(void);
static void dodge_update(int16_t dx, int16_t dy, int16_t z);
static void dodge_draw(void);

static void scroll_text_init(void);
static void scroll_text_update(int16_t dx, int16_t dy, int16_t z);
static void scroll_text_draw(void);

static void animation_init(void);
static void animation_update(int16_t dx, int16_t dy, int16_t z);
static void animation_draw(void);

static void tetris_init(void);
static void tetris_update(int16_t dx, int16_t dy, int16_t z);
static void tetris_draw(void);

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
};

// ============ Display Functions ============

static int get_led_index(int row, int col)
{
    return row * 8 + col;
}

static void set_pixel_at(int row, int col, uint8_t g, uint8_t r, uint8_t b)
{
    if (row >= 0 && row < 8 && col >= 0 && col < 8) {
        int index = get_led_index(row, col);
        led_data[index * 3 + 0] = g;
        led_data[index * 3 + 1] = r;
        led_data[index * 3 + 2] = b;
    }
}

static void clear_display(void)
{
    memset(led_data, 0, sizeof(led_data));
}

static void ws2812_init(void)
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

static void ws2812_send(void)
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

// ============ I2C / Accelerometer Functions ============

static void i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t adxl345_write_reg(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t adxl345_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t adxl345_init(void)
{
    uint8_t devid;
    esp_err_t ret = adxl345_read_reg(ADXL345_DEVID, &devid, 1);
    if (ret != ESP_OK) return ret;
    if (devid != 0xE5) {
        printf("ADXL345 wrong ID: 0x%02X (expected 0xE5)\n", devid);
        return ESP_ERR_NOT_FOUND;
    }

    ret = adxl345_write_reg(ADXL345_BW_RATE, 0x0A);
    if (ret != ESP_OK) return ret;

    ret = adxl345_write_reg(ADXL345_DATA_FORMAT, 0x09);
    if (ret != ESP_OK) return ret;

    ret = adxl345_write_reg(ADXL345_POWER_CTL, 0x08);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t adxl345_read(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t data[6];
    esp_err_t ret = adxl345_read_reg(ADXL345_DATAX0, data, 6);
    if (ret != ESP_OK) return ret;

    *x = (int16_t)((data[1] << 8) | data[0]);
    *y = (int16_t)((data[3] << 8) | data[2]);
    *z = (int16_t)((data[5] << 8) | data[4]);
    return ESP_OK;
}

static void calibrate_accel(void)
{
    int32_t sum_x = 0, sum_y = 0;
    int samples = 20;

    printf("Calibrating accelerometer - keep board level...\n");

    for (int i = 0; i < samples; i++) {
        int16_t x, y, z;
        if (adxl345_read(&x, &y, &z) == ESP_OK) {
            sum_x += x;
            sum_y += y;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    cal_x = sum_x / samples;
    cal_y = sum_y / samples;

    printf("Calibration done: x=%d, y=%d\n", cal_x, cal_y);
}

// ============ Flip Detection ============

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

// ============ Utility Functions ============

static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *g, uint8_t *r, uint8_t *b)
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

// ============ Ball with Trail Game ============

static void ball_trail_init(void)
{
    ball_x = 3;
    ball_y = 3;
    trail_head = 0;
    for (int i = 0; i < TRAIL_LENGTH; i++) {
        trail_buffer[i].x = ball_x;
        trail_buffer[i].y = ball_y;
    }
}

static void ball_trail_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    // Store current position in trail before moving
    trail_buffer[trail_head].x = ball_x;
    trail_buffer[trail_head].y = ball_y;
    trail_head = (trail_head + 1) % TRAIL_LENGTH;

    // Move ball - allow diagonal movement (X and Y independent)
    if (dx > TILT_THRESHOLD && ball_x < 7) ball_x++;
    if (dx < -TILT_THRESHOLD && ball_x > 0) ball_x--;

    if (dy > TILT_THRESHOLD && ball_y < 7) ball_y++;
    if (dy < -TILT_THRESHOLD && ball_y > 0) ball_y--;
}

static void ball_trail_draw(void)
{
    clear_display();

    // Draw trail (oldest to newest with increasing brightness)
    static const uint8_t brightness[] = {8, 15, 30, 50, 80, 120, 180, 255};
    for (int i = 0; i < TRAIL_LENGTH; i++) {
        int idx = (trail_head + i) % TRAIL_LENGTH;
        uint8_t b = brightness[i];
        set_pixel_at(trail_buffer[idx].y, trail_buffer[idx].x, b, b, b);
    }

    // Draw current ball (cyan/white)
    set_pixel_at(ball_y, ball_x, 0xFF, 0xFF, 0xFF);
}

// ============ Glow Ball Game ============

static void glow_ball_init(void)
{
    glow_ball_x = 3.5f;
    glow_ball_y = 3.5f;
}

static void glow_ball_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    // Smooth movement based on tilt
    float move_x = dx / 150.0f;
    float move_y = dy / 150.0f;

    glow_ball_x += move_x;
    glow_ball_y += move_y;

    // Clamp to display bounds
    if (glow_ball_x < 0.0f) glow_ball_x = 0.0f;
    if (glow_ball_x > 7.0f) glow_ball_x = 7.0f;
    if (glow_ball_y < 0.0f) glow_ball_y = 0.0f;
    if (glow_ball_y > 7.0f) glow_ball_y = 7.0f;
}

static void glow_ball_draw(void)
{
    clear_display();

    // Draw gradient glow around ball
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            // Calculate distance from ball (using float position)
            float dx = x - glow_ball_x;
            float dy = y - glow_ball_y;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist < 0.5f) {
                // Ball center - bright yellow
                set_pixel_at(y, x, 0x50, 0x50, 0x00);
            } else if (dist < 5.0f) {
                // Gradient glow - red/orange fading outward
                float intensity = (5.0f - dist) / 5.0f;
                intensity = intensity * intensity;  // Quadratic falloff for nicer glow

                uint8_t r = (uint8_t)(80.0f * intensity);
                uint8_t g = (uint8_t)(30.0f * intensity);
                uint8_t b = (uint8_t)(5.0f * intensity);

                // Add slight color shift based on position for more interest
                uint8_t hue_shift = ((x + y + (int)(anim_frame / 4)) * 8) & 0x1F;
                b += hue_shift / 2;

                set_pixel_at(y, x, g, r, b);
            }
        }
    }
}

// ============ Snake Game ============

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

static void snake_init(void)
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

static void snake_update(int16_t dx, int16_t dy, int16_t z)
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

static void snake_draw(void)
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

// ============ Maze Game ============

static bool maze_is_wall(int x, int y)
{
    if (x < 0 || x > 7 || y < 0 || y > 7) return true;
    return (maze[y] >> x) & 1;
}

static void maze_carve(int x, int y, uint8_t visited[8])
{
    visited[y] |= (1 << x);
    maze[y] &= ~(1 << x);  // Clear wall

    // Randomize directions
    int dirs[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int tmp = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = tmp;
    }

    static const int dx[] = {0, 1, 0, -1};
    static const int dy[] = {-1, 0, 1, 0};

    for (int i = 0; i < 4; i++) {
        int nx = x + dx[dirs[i]] * 2;
        int ny = y + dy[dirs[i]] * 2;

        if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && !(visited[ny] & (1 << nx))) {
            // Carve wall between
            int wx = x + dx[dirs[i]];
            int wy = y + dy[dirs[i]];
            maze[wy] &= ~(1 << wx);
            maze_carve(nx, ny, visited);
        }
    }
}

static void maze_generate(void)
{
    // Start with all walls
    memset(maze, 0xFF, sizeof(maze));

    uint8_t visited[8] = {0};

    // Carve from (0,0)
    maze_carve(0, 0, visited);

    // Ensure start and goal are clear
    maze[0] &= ~0x01;  // (0,0)
    maze[7] &= ~0x80;  // (7,7)

    // Ensure there's a path to goal by clearing some strategic cells
    maze[7] &= ~0x40;  // (6,7)
    maze[6] &= ~0x80;  // (7,6)
}

static void maze_init(void)
{
    maze_generate();
    maze_ball.x = 0;
    maze_ball.y = 0;
    maze_goal.x = 7;
    maze_goal.y = 7;
    maze_won = false;
    maze_win_counter = 0;
}

static void maze_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    if (maze_won) {
        if (++maze_win_counter > 40) {
            maze_init();
        }
        return;
    }

    // Check for victory
    if (maze_ball.x == maze_goal.x && maze_ball.y == maze_goal.y) {
        maze_won = true;
        maze_win_counter = 0;
        return;
    }

    // Move ball with wall collision
    int new_x = maze_ball.x;
    int new_y = maze_ball.y;

    if (dx > TILT_THRESHOLD) new_x++;
    else if (dx < -TILT_THRESHOLD) new_x--;

    if (dy > TILT_THRESHOLD) new_y++;
    else if (dy < -TILT_THRESHOLD) new_y--;

    // Only move if not hitting wall
    if (!maze_is_wall(new_x, maze_ball.y)) {
        maze_ball.x = new_x;
    }
    if (!maze_is_wall(maze_ball.x, new_y)) {
        maze_ball.y = new_y;
    }
}

static void maze_draw(void)
{
    clear_display();

    if (maze_won) {
        // Victory animation - rainbow wave spreading from goal
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                // Distance from goal for radial wave
                float dx = x - maze_goal.x;
                float dy = y - maze_goal.y;
                float dist = sqrtf(dx * dx + dy * dy);

                // Wave expands outward over time
                float wave_pos = maze_win_counter * 0.4f - dist * 1.5f;

                if (wave_pos > 0) {
                    // Hue shifts based on distance and time for rainbow effect
                    uint8_t hue = (uint8_t)((int)(dist * 25 + maze_win_counter * 3) & 0xFF);

                    // Brightness pulses gently and fades in at wave front
                    float pulse = (sinf(maze_win_counter * 0.12f + dist * 0.5f) + 1.0f) * 0.5f;
                    float fade_in = wave_pos < 8.0f ? wave_pos / 8.0f : 1.0f;
                    uint8_t brightness = (uint8_t)(32 + pulse * 28) * fade_in;

                    uint8_t g, r, b;
                    hsv_to_rgb(hue, 255, brightness, &g, &r, &b);
                    set_pixel_at(y, x, g, r, b);
                }
            }
        }
        return;
    }

    // Draw walls (blue)
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (maze_is_wall(x, y)) {
                set_pixel_at(y, x, 0x00, 0x00, 0x30);
            }
        }
    }

    // Draw goal (red, pulsing)
    uint8_t pulse = 0x20 + ((anim_frame % 20) * 2);
    set_pixel_at(maze_goal.y, maze_goal.x, 0x00, pulse, 0x00);

    // Draw ball (yellow)
    set_pixel_at(maze_ball.y, maze_ball.x, 0x40, 0x40, 0x00);
}

// ============ Breakout Game ============

static void breakout_init(void)
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

static void breakout_update(int16_t dx, int16_t dy, int16_t z)
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

static void breakout_draw(void)
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

// ============ Dodge Game ============

static void dodge_init(void)
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

static void dodge_update(int16_t dx, int16_t dy, int16_t z)
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

static void dodge_draw(void)
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

// ============ Scroll Text Game ============

// 5x7 font for letters (stored as 5 bytes per char, each byte is a column)
// Text: "Gelukkig Nieuwjaar 2026"
static const uint8_t font_5x7[][5] = {
    {0x3E, 0x41, 0x49, 0x49, 0x3A},  // G
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // l
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // k
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x0C, 0x52, 0x52, 0x52, 0x3E},  // g
    {0x00, 0x00, 0x00, 0x00, 0x00},  // (space)
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // w
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // j
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // r
    {0x00, 0x00, 0x00, 0x00, 0x00},  // (space)
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
};

#define SCROLL_TEXT_LEN 23
#define SCROLL_CHAR_WIDTH 6  // 5 pixels + 1 space
#define SCROLL_TOTAL_WIDTH (SCROLL_TEXT_LEN * SCROLL_CHAR_WIDTH)

static void scroll_text_init(void)
{
    scroll_pos = 8.0f;  // Start off-screen right
}

static void scroll_text_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dx; (void)dy; (void)z;

    // Scroll left
    scroll_pos -= 0.5f;

    // Loop back when text has fully scrolled off
    if (scroll_pos < -SCROLL_TOTAL_WIDTH) {
        scroll_pos = 8.0f;
    }
}

static void scroll_text_draw(void)
{
    clear_display();

    // Draw each column with sub-pixel fading
    for (int screen_x = 0; screen_x < 8; screen_x++) {
        // Calculate which text column this screen column corresponds to
        float text_col_f = screen_x - scroll_pos;
        int text_col = (int)text_col_f;
        float frac = text_col_f - text_col;  // Fractional part for fading

        // Draw two adjacent text columns blended together
        for (int blend = 0; blend < 2; blend++) {
            int col = text_col + blend;
            if (col < 0 || col >= SCROLL_TOTAL_WIDTH) continue;

            // Which character and which column within that character?
            int char_idx = col / SCROLL_CHAR_WIDTH;
            int char_col = col % SCROLL_CHAR_WIDTH;

            if (char_idx < 0 || char_idx >= SCROLL_TEXT_LEN) continue;
            if (char_col >= 5) continue;  // Space between characters

            uint8_t column_data = font_5x7[char_idx][char_col];

            // Calculate blend factor
            float blend_factor = (blend == 0) ? (1.0f - frac) : frac;
            uint8_t brightness = (uint8_t)(70.0f * blend_factor);

            // Draw the 7 rows of this column (centered vertically)
            for (int bit = 0; bit < 7; bit++) {
                if (column_data & (1 << bit)) {
                    int screen_y = bit;  // Top-aligned
                    if (screen_y >= 0 && screen_y < 8) {
                        // Add to existing pixel (for blending)
                        int idx = get_led_index(screen_y, screen_x);
                        uint8_t curr_g = led_data[idx * 3 + 0];
                        uint8_t curr_r = led_data[idx * 3 + 1];

                        // Orange/yellow color
                        uint8_t new_g = curr_g + brightness / 2;
                        uint8_t new_r = curr_r + brightness;
                        if (new_g > 70) new_g = 70;
                        if (new_r > 70) new_r = 70;

                        led_data[idx * 3 + 0] = new_g;
                        led_data[idx * 3 + 1] = new_r;
                        led_data[idx * 3 + 2] = 0;
                    }
                }
            }
        }
    }
}

// ============ Animation Mode ============

static void animation_init(void)
{
    anim_frame = 0;
    anim_cycle_counter = 0;
    current_anim = ANIM_SPIRAL;
}

static void animation_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dx; (void)dy; (void)z;

    anim_frame++;
    anim_cycle_counter++;

    if (anim_cycle_counter >= ANIM_CYCLE_FRAMES) {
        anim_cycle_counter = 0;
        current_anim = (current_anim + 1) % ANIM_COUNT;
    }
}

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

static void animation_draw(void)
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

// ============ Tetris Game ============

// Get piece cell at local coordinates (0-3, 0-3)
static bool tetris_get_piece_cell(int type, int rot, int lx, int ly)
{
    if (lx < 0 || lx > 3 || ly < 0 || ly > 3) return false;
    uint16_t shape = tetris_pieces[type][rot];
    int bit = (3 - ly) * 4 + (3 - lx);
    return (shape >> bit) & 1;
}

// Check if piece fits at position
static bool tetris_piece_fits(int type, int rot, int px, int py)
{
    for (int ly = 0; ly < 4; ly++) {
        for (int lx = 0; lx < 4; lx++) {
            if (tetris_get_piece_cell(type, rot, lx, ly)) {
                int bx = px + lx;
                int by = py + ly;
                // Check bounds
                if (bx < 0 || bx >= 8 || by >= 8) return false;
                // Check collision with board (only if on screen)
                if (by >= 0 && (tetris_board[by] & (1 << bx))) return false;
            }
        }
    }
    return true;
}

// Lock piece into board
static void tetris_lock_piece(void)
{
    for (int ly = 0; ly < 4; ly++) {
        for (int lx = 0; lx < 4; lx++) {
            if (tetris_get_piece_cell(tetris_piece_type, tetris_piece_rot, lx, ly)) {
                int bx = tetris_piece_x + lx;
                int by = tetris_piece_y + ly;
                if (by >= 0 && by < 8 && bx >= 0 && bx < 8) {
                    tetris_board[by] |= (1 << bx);
                }
            }
        }
    }
}

// Clear completed lines
static void tetris_clear_lines(void)
{
    for (int y = 7; y >= 0; y--) {
        if (tetris_board[y] == 0xFF) {
            // Line is full, shift everything down
            for (int sy = y; sy > 0; sy--) {
                tetris_board[sy] = tetris_board[sy - 1];
            }
            tetris_board[0] = 0;
            y++;  // Check this row again
        }
    }
}

// Spawn new piece
static void tetris_spawn_piece(void)
{
    tetris_piece_type = esp_random() % 7;
    tetris_piece_rot = 0;
    tetris_piece_x = 2;
    tetris_piece_y = -2;  // Start above screen

    // Check if spawn position is blocked
    if (!tetris_piece_fits(tetris_piece_type, tetris_piece_rot, tetris_piece_x, tetris_piece_y)) {
        tetris_game_over = true;
        tetris_anim_counter = 0;
    }
}

static void tetris_init(void)
{
    memset(tetris_board, 0, sizeof(tetris_board));
    tetris_game_over = false;
    tetris_anim_counter = 0;
    tetris_drop_counter = 0;
    tetris_move_counter = 0;
    tetris_last_dx = 0;
    tetris_spawn_piece();
}

static void tetris_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    if (tetris_game_over) {
        tetris_anim_counter++;
        if (tetris_anim_counter > 80) {
            tetris_init();
        }
        return;
    }

    // Movement with hysteresis
    tetris_last_dx = (tetris_last_dx * 2 + dx) / 3;

    tetris_move_counter++;
    if (tetris_move_counter >= TETRIS_MOVE_DELAY) {
        tetris_move_counter = 0;

        // Horizontal movement
        if (tetris_last_dx > TILT_THRESHOLD) {
            if (tetris_piece_fits(tetris_piece_type, tetris_piece_rot, tetris_piece_x + 1, tetris_piece_y)) {
                tetris_piece_x++;
            }
        } else if (tetris_last_dx < -TILT_THRESHOLD) {
            if (tetris_piece_fits(tetris_piece_type, tetris_piece_rot, tetris_piece_x - 1, tetris_piece_y)) {
                tetris_piece_x--;
            }
        }

        // Rotation (tilt forward)
        if (dy < -TILT_THRESHOLD * 2) {
            int new_rot = (tetris_piece_rot + 1) % 4;
            if (tetris_piece_fits(tetris_piece_type, new_rot, tetris_piece_x, tetris_piece_y)) {
                tetris_piece_rot = new_rot;
            }
        }
    }

    // Fast drop (tilt backward)
    int drop_speed = TETRIS_DROP_INTERVAL;
    if (dy > TILT_THRESHOLD * 2) {
        drop_speed = 2;
    }

    // Automatic drop
    tetris_drop_counter++;
    if (tetris_drop_counter >= drop_speed) {
        tetris_drop_counter = 0;

        if (tetris_piece_fits(tetris_piece_type, tetris_piece_rot, tetris_piece_x, tetris_piece_y + 1)) {
            tetris_piece_y++;
        } else {
            // Lock piece and spawn new one
            tetris_lock_piece();
            tetris_clear_lines();
            tetris_spawn_piece();
        }
    }
}

static void tetris_draw(void)
{
    clear_display();

    if (tetris_game_over) {
        // Game over animation - collapse effect
        int collapse_row = tetris_anim_counter / 8;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                if (y >= (7 - collapse_row)) {
                    // Red flash for collapsed rows
                    if ((tetris_anim_counter / 4) % 2 == 0) {
                        set_pixel_at(y, x, 0x00, 0x30, 0x00);
                    }
                } else if (tetris_board[y] & (1 << x)) {
                    // Show remaining board dimmed
                    set_pixel_at(y, x, 0x10, 0x10, 0x10);
                }
            }
        }
        return;
    }

    // Draw locked pieces (gray)
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (tetris_board[y] & (1 << x)) {
                set_pixel_at(y, x, 0x20, 0x20, 0x20);
            }
        }
    }

    // Draw current piece with its color
    for (int ly = 0; ly < 4; ly++) {
        for (int lx = 0; lx < 4; lx++) {
            if (tetris_get_piece_cell(tetris_piece_type, tetris_piece_rot, lx, ly)) {
                int bx = tetris_piece_x + lx;
                int by = tetris_piece_y + ly;
                if (bx >= 0 && bx < 8 && by >= 0 && by < 8) {
                    set_pixel_at(by, bx,
                        tetris_colors[tetris_piece_type][0],
                        tetris_colors[tetris_piece_type][1],
                        tetris_colors[tetris_piece_type][2]);
                }
            }
        }
    }
}

// ============ Main ============

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
