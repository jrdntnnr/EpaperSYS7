#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Lower-panel controls under the 512x342 guest viewport.
 *
 *   y=0    +------------------------------------------+
 *          |  14px margin                              |
 *   y=14   |  +------------------------------------+  |
 *          |  |      Macintosh 512x342 viewport     |  |
 *   y=356  |  +------------------------------------+  |
 *          |  [TRACKPAD] [KEYBOARD] [SYSTEM]  tabs     |
 *          |  status line                              |
 *          |  mode body: pad / keys / actions          |
 *   y=960  +------------------------------------------+
 */
enum { UI_TRACKPAD, UI_KEYBOARD, UI_SYSTEM, UI_MODES };
enum { UI_ACT_PAUSE, UI_ACT_RESET, UI_ACT_CLEAN, UI_ACT_STOP, UI_ACT_RETRY, UI_ACTIONS };
typedef enum {
    UI_HIT_NONE,
    UI_HIT_TAB,    /* value = mode */
    UI_HIT_PAD,    /* trackpad surface */
    UI_HIT_CLICK,  /* momentary mouse button */
    UI_HIT_DRAG,   /* drag lock toggle */
    UI_HIT_KEY,    /* value = Mac key code */
    UI_HIT_ACTION, /* value = UI_ACT_* */
    UI_HIT_SPEED   /* value = paper_epd_speed_t */
} ui_hit_kind_t;
typedef struct { ui_hit_kind_t kind; int value; } ui_hit_t;

void ui_init(void);
typedef struct {
    int mode;
    const char *status;
    bool running, drag;
    unsigned modifiers;
    int speed; /* paper_epd_speed_t */
} ui_state_t;
void ui_draw(const ui_state_t *s);
/* Bezel around the guest viewport; call after a full panel clean. */
void ui_frame(void);
void ui_message(const char *title, const char *line1, const char *line2, const char *line3);
void ui_publish(void);
/* Resolve a portrait touch point against the current mode's layout. */
ui_hit_t ui_hit(int mode, int x, int y);
