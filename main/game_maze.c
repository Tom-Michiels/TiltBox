#include <math.h>
#include <string.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

static uint8_t maze[8];
static position_t maze_ball;
static position_t maze_goal;
static bool maze_won = false;
static int maze_win_counter = 0;

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

void maze_init(void)
{
    maze_generate();
    maze_ball.x = 0;
    maze_ball.y = 0;
    maze_goal.x = 7;
    maze_goal.y = 7;
    maze_won = false;
    maze_win_counter = 0;
}

void maze_update(int16_t dx, int16_t dy, int16_t z)
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

void maze_draw(void)
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
