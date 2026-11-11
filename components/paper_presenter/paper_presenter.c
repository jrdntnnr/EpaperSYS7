#include "paper_presenter.h"
#include <string.h>

void paper_presenter_invalidate(paper_presenter_t *p)
{
    memset(p->valid, 0, sizeof(p->valid));
}
void paper_presenter_init(paper_presenter_t *p)
{
    memset(p, 0, sizeof(*p));
}
void paper_capture_frame(paper_render_line_fn render, uint8_t *frame)
{
    uint16_t line[PAPER_MAC_WIDTH];
    for (int y = 0; y < PAPER_MAC_HEIGHT; ++y) {
        render(y, line);
        for (int x = 0; x < PAPER_MAC_WIDTH; x += 8) {
            uint8_t bits = 0;
            for (int b = 0; b < 8; ++b)
                if (line[x + b] == 0) bits |= (uint8_t)(0x80u >> b);
            frame[y * PAPER_MAC_STRIDE + x / 8] = bits;
        }
    }
}
static bool changed(const paper_presenter_t *p, const uint8_t *frame,
                    int tx, int ty, int height)
{
    if (!p->valid[ty][tx]) return true;
    for (int row = ty * 8; row < ty * 8 + height; ++row) {
        int offset = row * PAPER_MAC_STRIDE + tx;
        if (frame[offset] != p->previous[offset]) return true;
    }
    return false;
}
bool paper_presenter_present(paper_presenter_t *p, const uint8_t *frame,
                             paper_submit_fn submit, void *context)
{
    for (int ty = 0; ty < 43; ++ty) {
        int height = ty == 42 ? 6 : 8;
        for (int tx = 0; tx < 64;) {
            if (!changed(p, frame, tx, ty, height)) { ++tx; continue; }
            int start = tx++;
            while (tx < 64 && changed(p, frame, tx, ty, height)) ++tx;
            int bytes = tx - start;
            size_t output = 0;
            for (int y = ty * 8; y < ty * 8 + height; ++y) {
                for (int x = start; x < tx; ++x) {
                    uint8_t input = frame[y * PAPER_MAC_STRIDE + x];
                    for (int half = 0; half < 2; ++half) {
                        uint8_t packed = 0;
                        for (int bit = 0; bit < 4; ++bit) {
                            unsigned shade = input & (0x80u >> (half * 4 + bit)) ? 0 : 3;
                            packed |= (uint8_t)(shade << (6 - bit * 2));
                        }
                        p->packed[output++] = packed;
                    }
                }
            }
            paper_rect_t rect = {(uint16_t)(PAPER_VIEW_X + start * 8),
                                 (uint16_t)(PAPER_VIEW_Y + ty * 8), (uint16_t)(bytes * 8),
                                 (uint16_t)height};
            if (!submit(context, rect, p->packed, output)) return false;
            for (int y = ty * 8; y < ty * 8 + height; ++y)
                memcpy(p->previous + y * 64 + start, frame + y * 64 + start, (size_t)bytes);
            memset(p->valid[ty] + start, 1, (size_t)bytes);
        }
    }
    return true;
}
