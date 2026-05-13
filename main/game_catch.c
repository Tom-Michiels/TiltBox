#include <stdint.h>
#include <stdbool.h>
#include "esp_random.h"
#include "game_common.h"
#include "display.h"

#define CATCH_MAX_ITEMS 6

typedef struct {
    float y;          // Y position (-1 = inactive)
    int x;            // Column (0-7)
    bool is_good;     // true = green (catch), false = red (avoid)
} catch_item_t;

static int catch_basket_x = 3;       // Basket center position (0-7), 3px wide at row 7
static int16_t catch_last_dx = 0;
static catch_item_t catch_items[CATCH_MAX_ITEMS];
static int catch_score = 0;
static int catch_lives = 3;
static bool catch_alive = true;
static int catch_anim_counter = 0;
static float catch_fall_speed = 0.12f;
static int catch_spawn_counter = 0;

void catch_game_init(void)
{
    catch_basket_x = 3;
    catch_last_dx = 0;
    catch_score = 0;
    catch_lives = 3;
    catch_alive = true;
    catch_anim_counter = 0;
    catch_fall_speed = 0.12f;
    catch_spawn_counter = 0;

    // Clear all items
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        catch_items[i].y = -1.0f;
        catch_items[i].x = 0;
        catch_items[i].is_good = true;
    }
}

void catch_game_update(int16_t dx, int16_t dy, int16_t z)
{
    (void)dy; (void)z;

    if (!catch_alive) {
        catch_anim_counter++;
        if (catch_anim_counter > 60) {
            catch_game_init();
        }
        return;
    }

    // Apply low-pass filter for basket movement
    catch_last_dx = (catch_last_dx * 3 + dx) / 4;

    // Map to position with hysteresis
    int target_x = 3 + (catch_last_dx / 20);
    if (target_x < 1) target_x = 1;
    if (target_x > 6) target_x = 6;

    int current_center = (catch_basket_x - 3) * 20;
    int hysteresis = 6;
    if (catch_last_dx > current_center + hysteresis ||
        catch_last_dx < current_center - hysteresis) {
        catch_basket_x = target_x;
    }

    // Update all active items
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        if (catch_items[i].y < 0) continue;

        catch_items[i].y += catch_fall_speed;

        // Check if item reached basket zone (row 7, 3px wide centered on basket_x)
        if (catch_items[i].y >= 6.5f && catch_items[i].y <= 7.5f &&
            catch_items[i].x >= catch_basket_x - 1 &&
            catch_items[i].x <= catch_basket_x + 1) {
            if (catch_items[i].is_good) {
                catch_score++;
            } else {
                catch_lives--;
                if (catch_lives <= 0) {
                    catch_alive = false;
                    catch_anim_counter = 0;
                }
            }
            catch_items[i].y = -1.0f;
            continue;
        }

        // Remove if off screen
        if (catch_items[i].y > 8.0f) {
            catch_items[i].y = -1.0f;
        }
    }

    // Spawn logic
    catch_spawn_counter++;
    int spawn_rate = 20 - (catch_score / 3);
    if (spawn_rate < 8) spawn_rate = 8;

    if (catch_spawn_counter >= spawn_rate) {
        catch_spawn_counter = 0;

        // Find empty slot
        for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
            if (catch_items[i].y < 0) {
                catch_items[i].x = esp_random() % 8;
                catch_items[i].y = 0.0f;
                catch_items[i].is_good = (esp_random() % 10) < 7; // 70% good, 30% bad
                break;
            }
        }
    }

    // Speed increase every 10 points
    if (catch_score > 0 && catch_score % 10 == 0) {
        catch_fall_speed += 0.01f;
        if (catch_fall_speed > 0.3f) catch_fall_speed = 0.3f;
    }
}

void catch_game_draw(void)
{
    clear_display();

    if (!catch_alive) {
        // Flash animation showing score as lit pixels
        if ((catch_anim_counter / 5) % 2 == 0) {
            for (int i = 0; i < 8 && i < catch_score; i++) {
                set_pixel_at(3, i, 0x30, 0x30, 0x00);
            }
        }
        return;
    }

    // Draw items
    for (int i = 0; i < CATCH_MAX_ITEMS; i++) {
        if (catch_items[i].y < 0) continue;
        int y = (int)catch_items[i].y;
        if (y < 0 || y >= 8) continue;

        if (catch_items[i].is_good) {
            set_pixel_at(y, catch_items[i].x, 0x30, 0x00, 0x00); // green
        } else {
            set_pixel_at(y, catch_items[i].x, 0x00, 0x30, 0x00); // red
        }
    }

    // Draw basket at row 7: 3 pixels centered, yellow
    for (int i = catch_basket_x - 1; i <= catch_basket_x + 1; i++) {
        if (i >= 0 && i < 8) {
            set_pixel_at(7, i, 0x20, 0x20, 0x00);
        }
    }

    // Draw lives: top-left pixels, green
    for (int i = 0; i < catch_lives && i < 3; i++) {
        set_pixel_at(0, i, 0x30, 0x00, 0x00);
    }

    // Draw score indicator: dim dots along top-right
    int dots = catch_score % 5;
    for (int i = 0; i < dots; i++) {
        set_pixel_at(0, 7 - i, 0x10, 0x10, 0x00);
    }
}
