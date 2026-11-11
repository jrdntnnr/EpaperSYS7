#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PAPER_MAC_WIDTH = 512, PAPER_MAC_HEIGHT = 342,
       PAPER_MAC_STRIDE = 64, PAPER_MAC_BYTES = 21888,
       PAPER_VIEW_X = 14, PAPER_VIEW_Y = 14,
       PAPER_PANEL_WIDTH = 540, PAPER_PANEL_HEIGHT = 960 };
typedef struct { uint16_t x, y, width, height; } paper_rect_t;
/* Packed 2bpp, MSB first, 0=black, 3=white; tightly packed rows.
 * True: backend copied bytes and guarantees eventual ordered presentation.
 * False: nothing accepted. Never wait for a physical panel refresh here.
 * On later hardware failure/queue loss, invalidate the presenter. */
typedef bool (*paper_submit_fn)(void *, paper_rect_t, const uint8_t *, size_t);
/* Compatible with mac_render_line_rgb565: black=0, white=0xffff. */
typedef void (*paper_render_line_fn)(int, uint16_t *);
typedef struct {
    uint8_t previous[PAPER_MAC_BYTES];
    uint8_t valid[43][64];
    uint8_t packed[512 * 8 / 4];
} paper_presenter_t;
/* Caller owns state (prefer PSRAM), frame and synchronization. Single consumer.
 * Snapshot must be stable during present; no pointers retained after return. */
void paper_presenter_init(paper_presenter_t *p);
void paper_presenter_invalidate(paper_presenter_t *p);
void paper_capture_frame(paper_render_line_fn render, uint8_t *frame);
/* False means backpressure. Accepted rectangles stay committed; retry with
 * the latest snapshot. Rejected regions are never marked clean. */
bool paper_presenter_present(paper_presenter_t *p, const uint8_t *frame,
                             paper_submit_fn submit, void *context);

#ifdef __cplusplus
}
#endif
