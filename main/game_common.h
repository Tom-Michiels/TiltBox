#ifndef GAME_COMMON_H
#define GAME_COMMON_H

#include <stdint.h>
#include <stdbool.h>

// Tilt threshold
#define TILT_THRESHOLD 25

// Flip detection
#define Z_POSITIVE_THRESHOLD  50
#define Z_NEGATIVE_THRESHOLD -50
#define FLIP_DEBOUNCE_COUNT   5

// Colors (GRB format)
#define BLACK  0x00, 0x00, 0x00
#define WHITE  0xFF, 0xFF, 0xFF
#define RED    0x00, 0x40, 0x00
#define GREEN  0x40, 0x00, 0x00
#define BLUE   0x00, 0x00, 0x40
#define YELLOW 0x40, 0x40, 0x00

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
    GAME_PONG,
    GAME_BALANCE,
    GAME_CATCH,
    GAME_TARGET,
    GAME_SOKOBAN,
    GAME_MARBLE_RACE,
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

// Shared animation frame counter (defined in main.c)
extern uint32_t anim_frame;

#endif // GAME_COMMON_H
