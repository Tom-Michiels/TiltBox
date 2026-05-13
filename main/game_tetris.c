#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

#define TETRIS_DROP_INTERVAL 12
#define TETRIS_MOVE_DELAY 3

static uint8_t tetris_board[8];
static int8_t tetris_piece_x, tetris_piece_y;
static int8_t tetris_piece_type;
static int8_t tetris_piece_rot;
static int tetris_drop_counter;
static int tetris_move_counter;
static bool tetris_game_over;
static int tetris_anim_counter;
static int16_t tetris_last_dx;

static const uint16_t tetris_pieces[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x0E40, 0x4C40, 0x4E00, 0x4640},
    {0x06C0, 0x8C40, 0x6C00, 0x4620},
    {0x0C60, 0x4C80, 0xC600, 0x2640},
    {0x0E80, 0xC440, 0x2E00, 0x44C0},
    {0x0E20, 0x44C0, 0x8E00, 0xC440},
};

static const uint8_t tetris_colors[7][3] = {
    {0x40, 0x00, 0x40},  // I - cyan
    {0x40, 0x40, 0x00},  // O - yellow
    {0x20, 0x00, 0x40},  // T - purple
    {0x40, 0x00, 0x00},  // S - green
    {0x00, 0x40, 0x00},  // Z - red
    {0x20, 0x40, 0x00},  // L - orange
    {0x00, 0x00, 0x40},  // J - blue
};

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

void tetris_init(void)
{
    memset(tetris_board, 0, sizeof(tetris_board));
    tetris_game_over = false;
    tetris_anim_counter = 0;
    tetris_drop_counter = 0;
    tetris_move_counter = 0;
    tetris_last_dx = 0;
    tetris_spawn_piece();
}

void tetris_update(int16_t dx, int16_t dy, int16_t z)
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

void tetris_draw(void)
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
