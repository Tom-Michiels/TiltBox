#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "game_common.h"
#include "display.h"

#define SOKOBAN_MAX_LEVELS  8
#define SOKOBAN_MAX_BLOCKS  4
#define SOKOBAN_MOVE_INTERVAL 8

static uint8_t soko_grid[8][8];       // Current state: 0=floor, 1=wall, 2=block
static bool soko_targets[8][8];       // True where a target exists
static int8_t soko_player_x, soko_player_y;
static int soko_level = 0;
static int soko_move_counter = 0;
static bool soko_won = false;
static int soko_win_counter = 0;

// Level format: 0=floor, 1=wall, 2=block, 3=target, 4=player, 5=block_on_target
static const uint8_t soko_levels[SOKOBAN_MAX_LEVELS][8][8] = {
    // Level 1: One block, simple push
    {
        {1,1,1,1,0,0,0,0},
        {1,0,0,1,0,0,0,0},
        {1,0,2,1,0,0,0,0},
        {1,0,0,0,0,0,0,0},
        {1,0,0,0,1,1,1,1},
        {1,0,4,0,0,3,0,1},
        {1,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1},
    },
    // Level 2: Two blocks
    {
        {1,1,1,1,1,0,0,0},
        {1,0,0,0,1,0,0,0},
        {1,0,2,0,1,1,1,1},
        {1,0,0,0,0,0,3,1},
        {1,1,0,1,1,0,0,1},
        {0,1,0,2,1,0,4,1},
        {0,1,3,0,0,0,0,1},
        {0,1,1,1,1,1,1,1},
    },
    // Level 3: L-shape push
    {
        {0,1,1,1,1,0,0,0},
        {0,1,0,0,1,0,0,0},
        {0,1,0,2,0,0,0,0},
        {0,1,0,0,0,0,0,0},
        {0,1,1,0,1,1,1,0},
        {0,0,1,0,0,3,1,0},
        {0,0,1,4,0,0,1,0},
        {0,0,1,1,1,1,1,0},
    },
    // Level 4: Two blocks, tight corridors
    {
        {1,1,1,1,1,1,1,0},
        {1,0,0,0,0,0,1,0},
        {1,0,2,1,0,0,1,0},
        {1,0,0,1,3,0,1,0},
        {1,1,0,1,0,1,1,0},
        {0,1,0,0,0,1,0,0},
        {0,1,0,2,3,4,1,0},
        {0,1,1,1,1,1,1,0},
    },
    // Level 5: Center maze
    {
        {1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,1},
        {1,0,1,0,1,0,0,1},
        {1,0,0,2,0,0,0,1},
        {1,0,1,0,1,3,0,1},
        {1,0,0,0,0,0,0,1},
        {1,4,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1},
    },
    // Level 6: Three blocks in a row
    {
        {1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,1},
        {1,0,2,2,2,0,0,1},
        {1,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,1},
        {1,3,3,3,0,0,4,1},
        {1,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1},
    },
    // Level 7: Winding path
    {
        {1,1,1,1,1,1,1,1},
        {1,4,0,0,1,0,0,1},
        {1,0,1,0,1,0,2,1},
        {1,0,1,0,0,0,0,1},
        {1,0,1,1,1,0,1,1},
        {1,0,0,0,0,0,0,1},
        {1,3,0,1,0,0,0,1},
        {1,1,1,1,1,1,1,1},
    },
    // Level 8: Final challenge
    {
        {1,1,1,1,1,1,1,1},
        {1,4,0,0,0,0,0,1},
        {1,0,1,1,1,1,0,1},
        {1,0,0,2,0,0,0,1},
        {1,1,1,1,1,0,1,1},
        {1,3,0,2,0,0,0,1},
        {1,3,0,0,0,3,0,1},
        {1,1,1,1,1,1,1,1},
    },
};

static bool soko_check_win(void)
{
    // All target positions must have a block
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (soko_targets[y][x] && soko_grid[y][x] != 2) {
                return false;
            }
        }
    }
    return true;
}

void sokoban_init(void)
{
    const uint8_t (*lvl)[8] = soko_levels[soko_level];
    memset(soko_targets, 0, sizeof(soko_targets));
    memset(soko_grid, 0, sizeof(soko_grid));

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t cell = lvl[y][x];
            switch (cell) {
                case 0: // floor
                    soko_grid[y][x] = 0;
                    break;
                case 1: // wall
                    soko_grid[y][x] = 1;
                    break;
                case 2: // block
                    soko_grid[y][x] = 2;
                    break;
                case 3: // target (empty)
                    soko_grid[y][x] = 0;
                    soko_targets[y][x] = true;
                    break;
                case 4: // player
                    soko_grid[y][x] = 0;
                    soko_player_x = x;
                    soko_player_y = y;
                    break;
                case 5: // block on target
                    soko_grid[y][x] = 2;
                    soko_targets[y][x] = true;
                    break;
                default:
                    soko_grid[y][x] = 0;
                    break;
            }
        }
    }

    soko_won = false;
    soko_win_counter = 0;
    soko_move_counter = 0;
}

void sokoban_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)z;

    if (soko_won) {
        soko_win_counter++;
        if (soko_win_counter > 40) {
            soko_level = (soko_level + 1) % SOKOBAN_MAX_LEVELS;
            sokoban_init();
        }
        return;
    }

    // Debounce movement
    soko_move_counter++;
    if (soko_move_counter < SOKOBAN_MOVE_INTERVAL) {
        return;
    }

    // Determine direction from tilt
    int8_t dir_x = 0, dir_y = 0;
    int16_t adx = dx < 0 ? -dx : dx;
    int16_t ady = dy < 0 ? -dy : dy;

    if (adx > TILT_THRESHOLD || ady > TILT_THRESHOLD) {
        if (adx > ady) {
            dir_x = (dx > 0) ? 1 : -1;
        } else {
            dir_y = (dy > 0) ? 1 : -1;
        }
    } else {
        return; // No significant tilt
    }

    // Reset move counter only when we actually process a move attempt
    soko_move_counter = 0;

    int8_t tx = soko_player_x + dir_x;
    int8_t ty = soko_player_y + dir_y;

    // Bounds check
    if (tx < 0 || tx > 7 || ty < 0 || ty > 7) return;

    uint8_t target = soko_grid[ty][tx];

    if (target == 1) {
        // Wall - can't move
        return;
    }

    if (target == 0) {
        // Floor (or target marker underneath) - move player
        soko_player_x = tx;
        soko_player_y = ty;
    } else if (target == 2) {
        // Block - check beyond
        int8_t bx = tx + dir_x;
        int8_t by = ty + dir_y;

        // Bounds check for beyond-block cell
        if (bx < 0 || bx > 7 || by < 0 || by > 7) return;

        uint8_t beyond = soko_grid[by][bx];
        if (beyond == 0) {
            // Push block: move block to beyond, move player to target cell
            soko_grid[by][bx] = 2;
            soko_grid[ty][tx] = 0;
            soko_player_x = tx;
            soko_player_y = ty;
        }
        // If beyond is wall or another block, can't push
    }

    // Check win condition
    if (soko_check_win()) {
        soko_won = true;
        soko_win_counter = 0;
    }
}

void sokoban_draw(void)
{
    clear_display();

    if (soko_won) {
        // Rainbow celebration animation
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                float dx = x - 3.5f;
                float dy = y - 3.5f;
                float dist = sqrtf(dx * dx + dy * dy);

                float wave_pos = soko_win_counter * 0.4f - dist * 1.5f;
                if (wave_pos > 0) {
                    uint8_t hue = (uint8_t)((int)(dist * 25 + soko_win_counter * 3) & 0xFF);
                    float pulse = (sinf(soko_win_counter * 0.12f + dist * 0.5f) + 1.0f) * 0.5f;
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

    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t cell = soko_grid[y][x];

            if (cell == 1) {
                // Wall - blue
                set_pixel_at(y, x, 0x00, 0x00, 0x30);
            } else if (soko_targets[y][x] && cell != 2) {
                // Target with no block - dim green
                set_pixel_at(y, x, 0x10, 0x08, 0x00);
            } else if (cell == 2 && soko_targets[y][x]) {
                // Block on target - bright green
                set_pixel_at(y, x, 0x30, 0x00, 0x00);
            } else if (cell == 2) {
                // Block (not on target) - orange
                set_pixel_at(y, x, 0x20, 0x20, 0x00);
            }
        }
    }

    // Draw player - white
    set_pixel_at(soko_player_y, soko_player_x, 0x40, 0x40, 0x40);
}
