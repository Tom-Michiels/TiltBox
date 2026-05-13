#include <math.h>
#include "game_common.h"
#include "display.h"

static float glow_ball_x = 3.5f;
static float glow_ball_y = 3.5f;

void glow_ball_init(void)
{
    glow_ball_x = 3.5f;
    glow_ball_y = 3.5f;
}

void glow_ball_update(int16_t dx, int16_t dy, int16_t z)
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

void glow_ball_draw(void)
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
