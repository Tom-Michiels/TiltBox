#include "game_common.h"
#include "display.h"

#define TRAIL_LENGTH 8
static position_t trail_buffer[TRAIL_LENGTH];
static int trail_head = 0;
static int ball_x = 3;
static int ball_y = 3;

void ball_trail_init(void)
{
    ball_x = 3;
    ball_y = 3;
    trail_head = 0;
    for (int i = 0; i < TRAIL_LENGTH; i++) {
        trail_buffer[i].x = ball_x;
        trail_buffer[i].y = ball_y;
    }
}

void ball_trail_update(int16_t dx, int16_t dy, int16_t z)
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

void ball_trail_draw(void)
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
