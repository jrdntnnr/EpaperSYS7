#include "ui.h"
#include "font_chicago12.h"
#include "paper_epd.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <assert.h>
#include <string.h>

/* Portrait canvas, 2bpp packed like the presenter. The panel is drawn in
 * 1-bit like the Mac above it; greys are Bayer patterns, never panel greys. */
enum { BLACK = 0, WHITE = 3 };
enum { PANEL_W = PAPER_PANEL_WIDTH, PANEL_H = PAPER_PANEL_HEIGHT, STRIDE = PANEL_W / 4 };

/* Layout, at the guest's pixel scale so Chicago here matches the Finder.
 *
 *   y=356  bezel bottom
 *   y=357  (*) Trackpad   ( ) Keyboard   ( ) System   (78px touch band)
 *   y=436  mode body
 *   y=946  bottom gutter
 */
enum {
    GUTTER = PAPER_VIEW_X,
    CONTENT_X = PAPER_VIEW_X,
    CONTENT_W = PAPER_MAC_WIDTH,
    VIEW_BOTTOM = PAPER_VIEW_Y + PAPER_MAC_HEIGHT,
    LOWER_H = PANEL_H - VIEW_BOTTOM,
    CONTENT_BOTTOM = PANEL_H - GUTTER,

    FONT_H = 15,
    LINE_H = 16,
    CORNER = 8,    /* FrameRoundRect(16, 16): System 7 push buttons */
    CONTROL = 12,  /* radio and checkbox glyphs */
    LABEL_GAP = 6, /* control glyph to its label */
    BUTTON_H = 44,
    ROW_PITCH = BUTTON_H + 12,

    /* The mode row is a full-width touch band from the bezel to the body. */
    MODE_Y = VIEW_BOTTOM + 1,
    MODE_H = 78,
    MODE_W = CONTENT_W / UI_MODES,
    BODY_Y = MODE_Y + MODE_H + 1,

    PAD_H = CONTENT_BOTTOM - BUTTON_H - GUTTER - BODY_Y,
    PAD_BUTTON_Y = BODY_Y + PAD_H + GUTTER,
    CLICK_W = 120,
    DRAG_X = CONTENT_X + CLICK_W + 2 * GUTTER,
    DRAG_W = 120,

    KEY_UNITS = 58, /* quarter keys per row: 13 keys + a 1.5 key */
    KEY_GAP = 4,
    KEY_H = 72,
    KEY_PITCH = KEY_H + 6,
    KEY_ROWS = 5,

    SYSTEM_BUTTON_W = 236,
    DISPLAY_X = CONTENT_X + SYSTEM_BUTTON_W + 40,
    DISPLAY_W = CONTENT_X + CONTENT_W - DISPLAY_X,
    STATUS_Y = CONTENT_BOTTOM - FONT_H,

    ALERT_INSET = 40,
    ALERT_Y = PAPER_VIEW_Y + 60,
    ALERT_H = 4 * 16 + 2 * 20,
    ALERT_PAD = 20,
    ALERT_FRAME_GAP = 3, /* dBoxProc: 1px line, white gap, 2px line */
    ALERT_FRAME_INNER = 2,

    /* Private label characters drawn as arrow glyphs. */
    ARROW_LEFT = 1, ARROW_RIGHT = 2, ARROW_UP = 3, ARROW_DOWN = 4,
    ARROW_W = 9, ARROW_H = 7, ARROW_ADVANCE = 11,
};

static const mac_font_t *font = &chicago_12;
static uint8_t *canvas;
static bool top_dirty, lower_dirty;

static const char *mode_names[UI_MODES] = {"Trackpad", "Keyboard", "System"};
static const char *speed_names[PAPER_SPEEDS] = {"Fast", "Balanced", "Sharp"};

/* Macintosh Plus keyboard, widths in quarter keys. */
typedef struct { const char *label; uint8_t code, units; } mac_key_t;
static const mac_key_t row_digits[] = {
    {"`", 0x32, 4}, {"1", 0x12, 4}, {"2", 0x13, 4}, {"3", 0x14, 4}, {"4", 0x15, 4},
    {"5", 0x17, 4}, {"6", 0x16, 4}, {"7", 0x1a, 4}, {"8", 0x1c, 4}, {"9", 0x19, 4},
    {"0", 0x1d, 4}, {"-", 0x1b, 4}, {"=", 0x18, 4}, {"Delete", 0x33, 6}};
static const mac_key_t row_upper[] = {
    {"Tab", 0x30, 6}, {"Q", 0x0c, 4}, {"W", 0x0d, 4}, {"E", 0x0e, 4}, {"R", 0x0f, 4},
    {"T", 0x11, 4}, {"Y", 0x10, 4}, {"U", 0x20, 4}, {"I", 0x22, 4}, {"O", 0x1f, 4},
    {"P", 0x23, 4}, {"[", 0x21, 4}, {"]", 0x1e, 4}, {"\\", 0x2a, 4}};
static const mac_key_t row_home[] = {
    {"Caps", 0x39, 7}, {"A", 0x00, 4}, {"S", 0x01, 4}, {"D", 0x02, 4}, {"F", 0x03, 4},
    {"G", 0x05, 4}, {"H", 0x04, 4}, {"J", 0x26, 4}, {"K", 0x28, 4}, {"L", 0x25, 4},
    {";", 0x29, 4}, {"'", 0x27, 4}, {"Return", 0x24, 7}};
static const mac_key_t row_lower[] = {
    {"Shift", 0x38, 9}, {"Z", 0x06, 4}, {"X", 0x07, 4}, {"C", 0x08, 4}, {"V", 0x09, 4},
    {"B", 0x0b, 4}, {"N", 0x2d, 4}, {"M", 0x2e, 4}, {",", 0x2b, 4}, {".", 0x2f, 4},
    {"/", 0x2c, 4}, {"Shift", 0x38, 9}};
static const mac_key_t row_space[] = {
    {"Esc", 0x35, 6}, {"Control", 0x3b, 7}, {"Option", 0x3a, 7}, {"\x11", 0x37, 6}, /* 0x11: Chicago's cloverleaf */
    {"", 0x31, 16}, {"\x01", 0x7b, 4}, {"\x02", 0x7c, 4}, {"\x03", 0x7e, 4}, {"\x04", 0x7d, 4}};
static const struct { const mac_key_t *keys; int count; } rows[KEY_ROWS] = {
    {row_digits, 14}, {row_upper, 14}, {row_home, 13}, {row_lower, 12}, {row_space, 9}};

/* Sticky modifiers in main.c's bit order: shift, control, option, command, caps. */
static const int modifier_codes[] = {0x38, 0x3b, 0x3a, 0x37, 0x39};

/* ---- Drawing primitives ---- */

static void pixel(int x, int y, int shade)
{
    if (x < 0 || x >= PANEL_W || y < 0 || y >= PANEL_H) { return; }
    unsigned shift = 6 - 2 * (x % 4);
    size_t i = (size_t)y * STRIDE + x / 4;
    canvas[i] = (canvas[i] & ~(3u << shift)) | ((unsigned)shade << shift);
}

static void hline(int x, int y, int w, int shade)
{
    for (int i = x; i < x + w; ++i) { pixel(i, y, shade); }
}

static void fill(int x, int y, int w, int h, int shade)
{
    for (int j = y; j < y + h; ++j) { hline(x, j, w, shade); }
}

/* Rectangle outline of thickness t. */
static void frame(int x, int y, int w, int h, int t)
{
    fill(x, y, w, t, BLACK);
    fill(x, y + h - t, w, t, BLACK);
    fill(x, y, t, h, BLACK);
    fill(x + w - t, y, t, h, BLACK);
}

/* Ordered dither: `level` of 16 pixels black. 8 is the desktop pattern. */
static void pattern(int x, int y, int w, int h, int level)
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int j = y; j < y + h; ++j) {
        for (int i = x; i < x + w; ++i) { pixel(i, j, bayer[j & 3][i & 3] < level ? BLACK : WHITE); }
    }
}

/* Corner inset for row j of a rounded rectangle of height h, radius r. */
static int inset(int j, int h, int r)
{
    int d = j < r ? r - 1 - j : j >= h - r ? j - (h - r) : -1;
    if (d < 0) { return 0; }
    int k = 0;
    while ((r - k) * (r - k) + d * d > r * r) { ++k; }
    return k;
}

/* Rounded rectangle: filled with `shade`, 1px border in `border`. */
static void rrect(int x, int y, int w, int h, int shade, int border)
{
    for (int j = 0; j < h; ++j) {
        int in = inset(j, h, CORNER);
        hline(x + in, y + j, w - 2 * in, shade);
        pixel(x + in, y + j, border);
        pixel(x + w - 1 - in, y + j, border);
        if (j == 0 || j == h - 1) { hline(x + in, y + j, w - 2 * in, border); }
    }
}

/* 12x12 control glyphs, one bit per column from the left. */
static const uint16_t radio_off[CONTROL] = {
    0x0F00, 0x30C0, 0x4020, 0x4020, 0x8010, 0x8010, 0x8010, 0x8010, 0x4020, 0x4020, 0x30C0, 0x0F00};
static const uint16_t radio_on[CONTROL] = {
    0x0F00, 0x30C0, 0x4020, 0x4F20, 0x9F90, 0x9F90, 0x9F90, 0x9F90, 0x4F20, 0x4020, 0x30C0, 0x0F00};
static const uint16_t check_off[CONTROL] = {
    0xFFF0, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0xFFF0};
static const uint16_t check_on[CONTROL] = {
    0xFFF0, 0xC030, 0xA050, 0x9090, 0x8910, 0x8610, 0x8610, 0x8910, 0x9090, 0xA050, 0xC030, 0xFFF0};

static void glyph12(int x, int y, const uint16_t *rows12)
{
    for (int j = 0; j < CONTROL; ++j) {
        for (int i = 0; i < CONTROL; ++i) {
            if (rows12[j] & (0x8000u >> i)) { pixel(x + i, y + j, BLACK); }
        }
    }
}

/* 9x7 arrows for the cursor keys, one bit per column from the left. */
static const uint16_t arrows[4][ARROW_H] = {
    {0x0800, 0x1000, 0x2000, 0x7F80, 0x2000, 0x1000, 0x0800},
    {0x0800, 0x0400, 0x0200, 0xFF00, 0x0200, 0x0400, 0x0800},
    {0x1000, 0x2800, 0x4400, 0x8200, 0x1000, 0x1000, 0x1000},
    {0x1000, 0x1000, 0x1000, 0x8200, 0x4400, 0x2800, 0x1000}};

static void arrow(int x, int y, int dir, int shade)
{
    for (int j = 0; j < ARROW_H; ++j) {
        for (int i = 0; i < ARROW_W; ++i) {
            if (arrows[dir][j] & (0x8000u >> i)) { pixel(x + i, y + j, shade); }
        }
    }
}

/* ---- Chicago text ---- */

typedef struct { int left, advance, col0, col1; } glyph_t;

static glyph_t glyph(unsigned c)
{
    unsigned missing = font->last + 1 - font->first;
    unsigned i = c < font->first || c > font->last ? missing : c - font->first;
    if (font->ow[i] == 0xFFFF) { i = missing; }
    uint16_t ow = font->ow[i];
    return (glyph_t){(int8_t)(ow >> 8) + font->kern_max, ow & 0xFF, font->loc[i], font->loc[i + 1]};
}

static int text_width(const char *s)
{
    int w = 0;
    for (; *s; ++s) {
        unsigned c = (unsigned char)*s;
        w += c <= ARROW_DOWN ? ARROW_ADVANCE : glyph(c).advance;
    }
    return w;
}

/* Draw a string with its glyph box top at y. */
static void text(int x, int y, const char *s, int shade)
{
    for (; *s; ++s) {
        unsigned c = (unsigned char)*s;
        if (c <= ARROW_DOWN) {
            arrow(x + 1, y + (FONT_H - ARROW_H) / 2, (int)c - ARROW_LEFT, shade);
            x += ARROW_ADVANCE;
            continue;
        }
        glyph_t g = glyph(c);
        for (int row = 0; row < font->height; ++row) {
            const uint8_t *bits = font->bits + row * font->row_bytes;
            for (int col = g.col0; col < g.col1; ++col) {
                if (bits[col / 8] & (0x80u >> (col % 8))) { pixel(x + g.left + col - g.col0, y + row, shade); }
            }
        }
        x += g.advance;
    }
}

/* Text centred in a box. */
static void text_box(int x, int y, int w, int h, const char *s, int shade)
{
    text(x + (w - text_width(s)) / 2, y + (h - FONT_H) / 2, s, shade);
}

/* System 7 push button; `on` is the hilited (inverted) state. */
static void button(int x, int y, int w, int h, const char *s, bool on)
{
    rrect(x, y, w, h, on ? BLACK : WHITE, BLACK);
    text_box(x, y, w, h, s, on ? WHITE : BLACK);
}

/* Radio button or checkbox with its label, vertically centred in h. */
static void control(int x, int y, int h, const char *s, const uint16_t *rows12)
{
    glyph12(x, y + (h - CONTROL) / 2, rows12);
    text(x + CONTROL + LABEL_GAP, y + (h - FONT_H) / 2, s, BLACK);
}

/* ---- Layout helpers shared by drawing and hit testing ---- */

static int mode_x(int i) { return CONTENT_X + i * MODE_W; }
static int key_x(int units) { return CONTENT_X + units * CONTENT_W / KEY_UNITS; }
static int key_y(int r) { return BODY_Y + r * KEY_PITCH; }
static int row_y(int i) { return BODY_Y + i * ROW_PITCH; }

static bool inside(int x, int y, int bx, int by, int bw, int bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* ---- Guest bezel ---- */

static void bezel_sides(void)
{
    hline(GUTTER - 1, PAPER_VIEW_Y - 1, CONTENT_W + 2, BLACK);
    for (int y = PAPER_VIEW_Y; y < VIEW_BOTTOM; ++y) {
        pixel(GUTTER - 1, y, BLACK);
        pixel(GUTTER + CONTENT_W, y, BLACK);
    }
}

/* ---- Mode bodies ---- */

static void draw_trackpad(bool drag)
{
    pattern(CONTENT_X, BODY_Y, CONTENT_W, PAD_H, 8);
    frame(CONTENT_X, BODY_Y, CONTENT_W, PAD_H, 1);

    button(CONTENT_X, PAD_BUTTON_Y, CLICK_W, BUTTON_H, "Click", false);
    control(DRAG_X, PAD_BUTTON_Y, BUTTON_H, "Drag lock", drag ? check_on : check_off);
}

static bool modifier_down(int code, unsigned modifiers)
{
    for (unsigned m = 0; m < sizeof(modifier_codes) / sizeof(*modifier_codes); ++m) {
        if (code == modifier_codes[m] && (modifiers & (1u << m))) { return true; }
    }
    return false;
}

static void draw_keyboard(unsigned modifiers)
{
    for (int r = 0; r < KEY_ROWS; ++r) {
        int units = 0;
        for (int k = 0; k < rows[r].count; ++k) {
            const mac_key_t *key = &rows[r].keys[k];
            int x = key_x(units), w = key_x(units + key->units) - x - KEY_GAP;
            button(x, key_y(r), w, KEY_H, key->label, modifier_down(key->code, modifiers));
            units += key->units;
        }
    }
}

static void draw_system(const ui_state_t *s)
{
    const char *names[UI_ACTIONS] = {
        s->running ? "Pause" : "Continue", "Restart", "Clean Screen", "Shut Down", "Retry"};
    for (int i = 0; i < UI_ACTIONS; ++i) {
        button(CONTENT_X, row_y(i), SYSTEM_BUTTON_W, BUTTON_H, names[i], false);
    }

    text(DISPLAY_X, row_y(0) + (BUTTON_H - FONT_H) / 2, "Display", BLACK);
    for (int i = 0; i < PAPER_SPEEDS; ++i) {
        control(DISPLAY_X, row_y(i + 1), BUTTON_H, speed_names[i], s->speed == i ? radio_on : radio_off);
    }
    text(CONTENT_X, STATUS_Y, s->status, BLACK);
}

/* ---- Public API ---- */

void ui_init(void)
{
    canvas = heap_caps_malloc((size_t)PANEL_H * STRIDE, MALLOC_CAP_SPIRAM);
    assert(canvas);
    memset(canvas, 255, (size_t)PANEL_H * STRIDE);
}

void ui_draw(const ui_state_t *s)
{
    memset(canvas + (size_t)VIEW_BOTTOM * STRIDE, 255, (size_t)LOWER_H * STRIDE);
    hline(GUTTER - 1, VIEW_BOTTOM, CONTENT_W + 2, BLACK); /* bezel bottom */

    for (int i = 0; i < UI_MODES; ++i) {
        control(mode_x(i), MODE_Y, MODE_H, mode_names[i], s->mode == i ? radio_on : radio_off);
    }

    if (s->mode == UI_TRACKPAD) {
        draw_trackpad(s->drag);
    } else if (s->mode == UI_KEYBOARD) {
        draw_keyboard(s->modifiers);
    } else {
        draw_system(s);
    }
    lower_dirty = true;
}

void ui_frame(void)
{
    memset(canvas, 255, (size_t)VIEW_BOTTOM * STRIDE);
    bezel_sides();
    top_dirty = true;
}

/* Modal alert in the viewport: dBoxProc double frame, four lines of Chicago. */
void ui_message(const char *title, const char *a, const char *b, const char *c)
{
    memset(canvas, 255, (size_t)VIEW_BOTTOM * STRIDE);
    bezel_sides();
    int x = CONTENT_X + ALERT_INSET, w = CONTENT_W - 2 * ALERT_INSET;
    frame(x, ALERT_Y, w, ALERT_H, 1);
    frame(x + ALERT_FRAME_GAP, ALERT_Y + ALERT_FRAME_GAP, w - 2 * ALERT_FRAME_GAP,
          ALERT_H - 2 * ALERT_FRAME_GAP, ALERT_FRAME_INNER);

    const char *lines[] = {title, a, b, c};
    for (int i = 0; i < 4; ++i) {
        text(x + ALERT_PAD, ALERT_Y + ALERT_PAD + i * LINE_H, lines[i], BLACK);
    }
    top_dirty = true;
}

void ui_publish(void)
{
    if (top_dirty) {
        paper_rect_t top = {0, 0, PANEL_W, VIEW_BOTTOM};
        while (!paper_epd_submit(NULL, top, canvas, (size_t)VIEW_BOTTOM * STRIDE)) { vTaskDelay(1); }
        top_dirty = false;
    }
    if (lower_dirty) {
        paper_rect_t lower = {0, VIEW_BOTTOM, PANEL_W, LOWER_H};
        while (!paper_epd_submit(NULL, lower, canvas + (size_t)VIEW_BOTTOM * STRIDE, (size_t)LOWER_H * STRIDE)) {
            vTaskDelay(1);
        }
        lower_dirty = false;
    }
    paper_epd_flush_async();
}

static ui_hit_t hit_keyboard(int x, int y)
{
    for (int r = 0; r < KEY_ROWS; ++r) {
        if (y < key_y(r) || y >= key_y(r) + KEY_H) { continue; }
        int units = 0;
        for (int k = 0; k < rows[r].count; ++k) {
            const mac_key_t *key = &rows[r].keys[k];
            if (x >= key_x(units) && x < key_x(units + key->units)) { return (ui_hit_t){UI_HIT_KEY, key->code}; }
            units += key->units;
        }
    }
    return (ui_hit_t){UI_HIT_NONE, 0};
}

static ui_hit_t hit_system(int x, int y)
{
    for (int i = 0; i < UI_ACTIONS; ++i) {
        if (inside(x, y, CONTENT_X, row_y(i), SYSTEM_BUTTON_W, BUTTON_H)) { return (ui_hit_t){UI_HIT_ACTION, i}; }
    }
    for (int i = 0; i < PAPER_SPEEDS; ++i) {
        if (inside(x, y, DISPLAY_X, row_y(i + 1), DISPLAY_W, BUTTON_H)) { return (ui_hit_t){UI_HIT_SPEED, i}; }
    }
    return (ui_hit_t){UI_HIT_NONE, 0};
}

ui_hit_t ui_hit(int mode, int x, int y)
{
    if (y >= MODE_Y && y < MODE_Y + MODE_H) {
        int i = x < CONTENT_X ? 0 : (x - CONTENT_X) / MODE_W;
        return (ui_hit_t){UI_HIT_TAB, i < UI_MODES ? i : UI_MODES - 1};
    }
    if (mode == UI_KEYBOARD) { return hit_keyboard(x, y); }
    if (mode == UI_SYSTEM) { return hit_system(x, y); }

    if (inside(x, y, CONTENT_X, BODY_Y, CONTENT_W, PAD_H)) { return (ui_hit_t){UI_HIT_PAD, 0}; }
    if (inside(x, y, CONTENT_X, PAD_BUTTON_Y, CLICK_W, BUTTON_H)) { return (ui_hit_t){UI_HIT_CLICK, 0}; }
    if (inside(x, y, DRAG_X, PAD_BUTTON_Y, DRAG_W, BUTTON_H)) { return (ui_hit_t){UI_HIT_DRAG, 0}; }
    return (ui_hit_t){UI_HIT_NONE, 0};
}
