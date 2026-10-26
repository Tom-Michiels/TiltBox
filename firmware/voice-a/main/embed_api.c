/*
 * embed_api.c - bridge between the MicroPython embed port and the firmware.
 *
 * Globals registered in __main__ (C side):
 *   matrix_set/get/fill/clear/show/brightness/icon/char/text/bitmap
 *   sleep_ms(ms), millis(), tilt() -> (x,y,z), gyro() -> (gx,gy,gz)
 * Plus a Python prelude (below) with time/random/math shims, hsv(),
 * tilt_dir(), shake(), matrix_rect/line/digit/number.
 *
 * Every embed_run() gets a fresh interpreter (init -> exec -> deinit).
 * Abort: the VM hook polls hal_abort_requested() (button, flip, serial 'x')
 * and schedules a KeyboardInterrupt; sleep_ms() checks the same.
 */
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <micropython_embed.h>
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "py/obj.h"

extern void     hal_matrix_set(int x, int y, uint8_t r, uint8_t g, uint8_t b);
extern void     hal_matrix_get(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);
extern void     hal_matrix_fill(uint8_t r, uint8_t g, uint8_t b);
extern void     hal_matrix_show(void);
extern void     hal_matrix_brightness(uint8_t n);
extern void     hal_matrix_bitmap(const uint8_t rows[8], uint8_t r, uint8_t g, uint8_t b);
extern void     hal_matrix_char(char c, uint8_t r, uint8_t g, uint8_t b);
extern int      hal_matrix_icon(const char *name, uint8_t r, uint8_t g, uint8_t b);
extern void     hal_matrix_text(const char *s, uint8_t r, uint8_t g, uint8_t b);
extern uint32_t hal_millis(void);
extern void     hal_delay_ms(uint32_t ms);
extern int      hal_abort_requested(void);
extern void     hal_serial_write(const char *s, size_t len);
extern void     hal_tilt(int *x, int *y, int *z);
extern void     hal_gyro(int *x, int *y, int *z);

/* GC heap in PSRAM: 2 MB. Internal RAM stays free for Wi-Fi/TLS. */
static char *mp_heap;
static const size_t MP_HEAP_LEN = 2 * 1024 * 1024;
static int      embed_active   = 0;
static uint32_t embed_deadline = 0;

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) { hal_serial_write(str, len); }
void mp_hal_set_interrupt_char(int c) { (void)c; }

void embed_vm_hook(void) {
    if (!embed_active) return;
    if (hal_abort_requested() || (embed_deadline != 0 && (int32_t)(hal_millis() - embed_deadline) >= 0)) {
        mp_sched_keyboard_interrupt();
    }
}

static uint8_t clamp_u8(mp_int_t v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

static mp_obj_t py_matrix_set(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_int_t x = mp_obj_get_int(args[0]), y = mp_obj_get_int(args[1]);
    if (x < 0 || x > 7 || y < 0 || y > 7) return mp_const_none;
    hal_matrix_set((int)x, (int)y, clamp_u8(mp_obj_get_int(args[2])), clamp_u8(mp_obj_get_int(args[3])), clamp_u8(mp_obj_get_int(args[4])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_matrix_set_obj, 5, 5, py_matrix_set);

static mp_obj_t py_matrix_get(mp_obj_t x_in, mp_obj_t y_in) {
    mp_int_t x = mp_obj_get_int(x_in), y = mp_obj_get_int(y_in);
    uint8_t r = 0, g = 0, b = 0;
    if (x >= 0 && x <= 7 && y >= 0 && y <= 7) hal_matrix_get((int)x, (int)y, &r, &g, &b);
    mp_obj_t t[3] = { MP_OBJ_NEW_SMALL_INT(r), MP_OBJ_NEW_SMALL_INT(g), MP_OBJ_NEW_SMALL_INT(b) };
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_matrix_get_obj, py_matrix_get);

static mp_obj_t py_matrix_fill(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
    hal_matrix_fill(clamp_u8(mp_obj_get_int(r)), clamp_u8(mp_obj_get_int(g)), clamp_u8(mp_obj_get_int(b)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_matrix_fill_obj, py_matrix_fill);
static mp_obj_t py_matrix_clear(void) { hal_matrix_fill(0, 0, 0); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_0(py_matrix_clear_obj, py_matrix_clear);
static mp_obj_t py_matrix_show(void) { hal_matrix_show(); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_0(py_matrix_show_obj, py_matrix_show);
static mp_obj_t py_matrix_brightness(mp_obj_t n) { hal_matrix_brightness(clamp_u8(mp_obj_get_int(n))); return mp_const_none; }
static MP_DEFINE_CONST_FUN_OBJ_1(py_matrix_brightness_obj, py_matrix_brightness);

static mp_obj_t py_matrix_icon(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    hal_matrix_icon(mp_obj_str_get_str(args[0]), clamp_u8(mp_obj_get_int(args[1])), clamp_u8(mp_obj_get_int(args[2])), clamp_u8(mp_obj_get_int(args[3])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_matrix_icon_obj, 4, 4, py_matrix_icon);
static mp_obj_t py_matrix_char(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    const char *s = mp_obj_str_get_str(args[0]);
    hal_matrix_char(s[0] ? s[0] : '?', clamp_u8(mp_obj_get_int(args[1])), clamp_u8(mp_obj_get_int(args[2])), clamp_u8(mp_obj_get_int(args[3])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_matrix_char_obj, 4, 4, py_matrix_char);
static mp_obj_t py_matrix_text(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    hal_matrix_text(mp_obj_str_get_str(args[0]), clamp_u8(mp_obj_get_int(args[1])), clamp_u8(mp_obj_get_int(args[2])), clamp_u8(mp_obj_get_int(args[3])));
    mp_handle_pending(true);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_matrix_text_obj, 4, 4, py_matrix_text);
static mp_obj_t py_matrix_bitmap(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    size_t len; mp_obj_t *items;
    mp_obj_get_array(args[0], &len, &items);
    uint8_t rows[8] = {0};
    for (size_t i = 0; i < 8 && i < len; i++) rows[i] = (uint8_t)(mp_obj_get_int(items[i]) & 0xFF);
    hal_matrix_bitmap(rows, clamp_u8(mp_obj_get_int(args[1])), clamp_u8(mp_obj_get_int(args[2])), clamp_u8(mp_obj_get_int(args[3])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_matrix_bitmap_obj, 4, 4, py_matrix_bitmap);

static mp_obj_t py_sleep_ms(mp_obj_t ms_in) {
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms < 0) ms = 0;
    const uint32_t end = hal_millis() + (uint32_t)ms;
    do {
        embed_vm_hook();
        mp_handle_pending(true);
        if ((int32_t)(end - hal_millis()) <= 0) break;
        hal_delay_ms(2);
    } while (1);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_sleep_ms_obj, py_sleep_ms);
static mp_obj_t py_millis(void) { return mp_obj_new_int_from_uint(hal_millis()); }
static MP_DEFINE_CONST_FUN_OBJ_0(py_millis_obj, py_millis);
static mp_obj_t py_tilt(void) {
    int x, y, z; hal_tilt(&x, &y, &z);
    mp_obj_t a[3] = { mp_obj_new_int(x), mp_obj_new_int(y), mp_obj_new_int(z) };
    return mp_obj_new_tuple(3, a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_tilt_obj, py_tilt);
static mp_obj_t py_gyro(void) {
    int x, y, z; hal_gyro(&x, &y, &z);
    mp_obj_t a[3] = { mp_obj_new_int(x), mp_obj_new_int(y), mp_obj_new_int(z) };
    return mp_obj_new_tuple(3, a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_gyro_obj, py_gyro);

static void embed_register_api(void) {
    static const struct { const char *name; const mp_obj_t obj; } api[] = {
        { "matrix_set", MP_OBJ_FROM_PTR(&py_matrix_set_obj) }, { "matrix_get", MP_OBJ_FROM_PTR(&py_matrix_get_obj) },
        { "matrix_fill", MP_OBJ_FROM_PTR(&py_matrix_fill_obj) }, { "matrix_clear", MP_OBJ_FROM_PTR(&py_matrix_clear_obj) },
        { "matrix_show", MP_OBJ_FROM_PTR(&py_matrix_show_obj) }, { "matrix_brightness", MP_OBJ_FROM_PTR(&py_matrix_brightness_obj) },
        { "matrix_icon", MP_OBJ_FROM_PTR(&py_matrix_icon_obj) }, { "matrix_char", MP_OBJ_FROM_PTR(&py_matrix_char_obj) },
        { "matrix_text", MP_OBJ_FROM_PTR(&py_matrix_text_obj) }, { "matrix_bitmap", MP_OBJ_FROM_PTR(&py_matrix_bitmap_obj) },
        { "sleep_ms", MP_OBJ_FROM_PTR(&py_sleep_ms_obj) }, { "millis", MP_OBJ_FROM_PTR(&py_millis_obj) },
        { "tilt", MP_OBJ_FROM_PTR(&py_tilt_obj) }, { "gyro", MP_OBJ_FROM_PTR(&py_gyro_obj) },
    };
    for (size_t i = 0; i < sizeof api / sizeof api[0]; i++) mp_store_global(qstr_from_str(api[i].name), api[i].obj);
}

/* Prelude: runs before every script in the same interpreter as a separate
   exec, so traceback line numbers of user code stay correct. Provides the
   modules an LLM reaches for (time, random; math is built in) and small drawing/motion
   helpers that keep generated games short and correct. */
static const char PRELUDE[] =
"class _T:\n"
"    def sleep(self, s): sleep_ms(int(s * 1000))\n"
"    def sleep_ms(self, m): sleep_ms(int(m))\n"
"    def sleep_us(self, u): sleep_ms(int(u) // 1000)\n"
"    def ticks_ms(self): return millis()\n"
"    def ticks_us(self): return millis() * 1000\n"
"    def ticks_diff(self, a, b): return a - b\n"
"    def ticks_add(self, a, b): return a + b\n"
"    def time(self): return millis() // 1000\n"
"    def monotonic(self): return millis() / 1000\n"
"time = _T()\n"
"utime = time\n"
"class _R:\n"
"    def __init__(self): self.s = (millis() * 2654435761) & 0x3FFFFFFF | 1\n"
"    def _n(self):\n"
"        self.s = (self.s * 1103515245 + 12345) & 0x3FFFFFFF\n"
"        return self.s >> 4\n"
"    def seed(self, x=None): self.s = ((int(x) if x else millis()) & 0x3FFFFFFF) | 1\n"
"    def getrandbits(self, n): return self._n() & ((1 << n) - 1)\n"
"    def randint(self, a, b): return a + self._n() % (b - a + 1)\n"
"    def randrange(self, a, b=None, step=1):\n"
"        if b is None: a, b = 0, a\n"
"        return a + step * (self._n() % ((b - a + step - 1) // step))\n"
"    def choice(self, q): return q[self._n() % len(q)]\n"
"    def random(self): return (self._n() & 0xFFFFFF) / 16777216.0\n"
"    def uniform(self, a, b): return a + (b - a) * self.random()\n"
"    def shuffle(self, q):\n"
"        for i in range(len(q) - 1, 0, -1):\n"
"            j = self._n() % (i + 1)\n"
"            q[i], q[j] = q[j], q[i]\n"
"random = _R()\n"
"urandom = random\n"
"randint = random.randint\n"
"randrange = random.randrange\n"
"choice = random.choice\n"
"shuffle = random.shuffle\n"
"sleep = time.sleep\n"
"ticks_ms = millis\n"
"import math\n"   /* math is compiled in (CORE level); time/random are not and get shims above */

"def hsv(h, s, v):\n"
"    h = int(h) & 255; s = int(s); v = int(v)\n"
"    if s <= 0: return (v, v, v)\n"
"    r = h // 43; f = (h - r * 43) * 6\n"
"    p = (v * (255 - s)) >> 8\n"
"    q = (v * (255 - ((s * f) >> 8))) >> 8\n"
"    t = (v * (255 - ((s * (255 - f)) >> 8))) >> 8\n"
"    if r == 0: return (v, t, p)\n"
"    if r == 1: return (q, v, p)\n"
"    if r == 2: return (p, v, t)\n"
"    if r == 3: return (p, q, v)\n"
"    if r == 4: return (t, p, v)\n"
"    return (v, p, q)\n"
"def tilt_dir(threshold=40):\n"
"    x, y, z = tilt()\n"
"    ax = -x if x < 0 else x; ay = -y if y < 0 else y\n"
"    if ax < threshold and ay < threshold: return ''\n"
"    if ax >= ay: return 'right' if x > 0 else 'left'\n"
"    return 'down' if y > 0 else 'up'\n"
"_shake_prev = [0, 0, 256]\n"
"def shake():\n"
"    x, y, z = tilt()\n"
"    m = int(math.sqrt(x * x + y * y + z * z)) - 256\n"
"    p = _shake_prev\n"
"    d = abs(x - p[0]) + abs(y - p[1]) + abs(z - p[2])\n"
"    p[0] = x; p[1] = y; p[2] = z\n"
"    return max(abs(m), d // 2)\n"
"def matrix_rect(x, y, w, h, r, g, b):\n"
"    for yy in range(int(y), int(y + h)):\n"
"        for xx in range(int(x), int(x + w)):\n"
"            matrix_set(xx, yy, r, g, b)\n"
"def matrix_line(x0, y0, x1, y1, r, g, b):\n"
"    x0 = int(x0); y0 = int(y0); x1 = int(x1); y1 = int(y1)\n"
"    dx = abs(x1 - x0); dy = -abs(y1 - y0)\n"
"    sx = 1 if x0 < x1 else -1; sy = 1 if y0 < y1 else -1\n"
"    err = dx + dy\n"
"    for _ in range(32):\n"
"        matrix_set(x0, y0, r, g, b)\n"
"        if x0 == x1 and y0 == y1: break\n"
"        e2 = 2 * err\n"
"        if e2 >= dy: err += dy; x0 += sx\n"
"        if e2 <= dx: err += dx; y0 += sy\n"
"_DIGITS = (0o75557, 0o26222, 0o71747, 0o71717, 0o55711, 0o74717, 0o74757, 0o71111, 0o75757, 0o75717)\n"
"def matrix_digit(d, x, y, r, g, b):\n"
"    bits = _DIGITS[int(d) % 10]\n"
"    for row in range(5):\n"
"        v = (bits >> (3 * (4 - row))) & 7\n"
"        for col in range(3):\n"
"            if v & (4 >> col): matrix_set(x + col, y + row, r, g, b)\n"
"def matrix_number(n, r, g, b):\n"
"    n = int(n)\n"
"    if n < 0: n = 0\n"
"    if n > 99: n = 99\n"
"    if n < 10: matrix_digit(n, 3, 1, r, g, b)\n"
"    else:\n"
"        matrix_digit(n // 10, 0, 1, r, g, b)\n"
"        matrix_digit(n % 10, 4, 1, r, g, b)\n";

const char *embed_prelude(void) { return PRELUDE; }

/* budget_ms = 0: no time limit; the script stops via the button, flip or 'x'. */
void embed_run(const char *code, uint32_t budget_ms) {
    int stack_top;
    if (!mp_heap) mp_heap = heap_caps_malloc(MP_HEAP_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mp_heap) { hal_serial_write("Python heap unavailable\n", 24); return; }
    mp_embed_init(mp_heap, MP_HEAP_LEN, &stack_top);
    /* Main task stack is 48 kB (sdkconfig); keep a wide margin so deep
       recursion raises RecursionError instead of overflowing the task. */
    mp_stack_set_limit(28 * 1024);
    embed_register_api();
    mp_embed_exec_str(PRELUDE);
    embed_deadline = (budget_ms == 0) ? 0 : hal_millis() + budget_ms;
    embed_active = 1;
    mp_embed_exec_str(code);
    embed_active = 0;
    embed_deadline = 0;
    mp_embed_deinit();
}
