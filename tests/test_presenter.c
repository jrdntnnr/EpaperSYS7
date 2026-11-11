#include "paper_presenter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static paper_presenter_t presenter;
static uint8_t frame[PAPER_MAC_BYTES];
static uint8_t panel[540 * 960];
static int calls, reject_at;
static bool submit(void *context, paper_rect_t r, const uint8_t *data, size_t size)
{
    (void)context;
    ++calls;
    if (calls == reject_at) return false;
    assert(r.x >= 14 && r.x + r.width <= 526);
    assert(r.y >= 14 && r.y + r.height <= 356);
    assert(size == (size_t)r.width * r.height / 4);
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x)
            panel[(r.y + y) * 540 + r.x + x] =
                (data[y * (r.width / 4) + x / 4] >> (6 - 2 * (x % 4))) & 3;
    return true;
}
static void verify(void)
{
    for (int y = 0; y < 960; ++y)
        for (int x = 0; x < 540; ++x) {
            int expected = 7;
            if (y >= 14 && y < 356 && x >= 14 && x < 526)
                expected = frame[(y - 14) * 64 + (x - 14) / 8] & (0x80 >> ((x - 14) % 8)) ? 0 : 3;
            assert(panel[y * 540 + x] == expected);
        }
}
static void render(int y, uint16_t *line)
{
    for (int x = 0; x < 512; ++x) line[x] = (x + y) % 3 ? 0xffff : 0;
}
int main(void)
{
    memset(panel, 7, sizeof(panel));
    paper_presenter_init(&presenter);
    assert(paper_presenter_present(&presenter, frame, submit, NULL));
    assert(calls == 43); verify();
    calls = 0;
    assert(paper_presenter_present(&presenter, frame, submit, NULL));
    assert(calls == 0);
    frame[0] = 0x80; frame[21887] = 1;
    reject_at = 2;
    assert(!paper_presenter_present(&presenter, frame, submit, NULL));
    calls = 0; reject_at = 0;
    assert(paper_presenter_present(&presenter, frame, submit, NULL));
    assert(calls == 1); verify();
    /* Deterministic random snapshots, failures, latest-frame replacement. */
    uint32_t seed = 123;
    for (int iteration = 0; iteration < 100; ++iteration) {
        for (int i = 0; i < 200; ++i) {
            seed = seed * 1664525u + 1013904223u;
            frame[seed % PAPER_MAC_BYTES] ^= (uint8_t)(seed >> 24);
        }
        calls = 0; reject_at = 3;
        (void)paper_presenter_present(&presenter, frame, submit, NULL);
        frame[21887] ^= 0x55;
        calls = 0; reject_at = 0;
        assert(paper_presenter_present(&presenter, frame, submit, NULL)); verify();
    }
    paper_capture_frame(render, frame);
    for (int y = 0; y < 342; ++y)
        for (int x = 0; x < 512; ++x)
            assert(!!(frame[y * 64 + x / 8] & (0x80 >> (x % 8))) == ((x + y) % 3 == 0));
    paper_presenter_invalidate(&presenter);
    calls = 0;
    assert(paper_presenter_present(&presenter, frame, submit, NULL));
    assert(calls == 43); verify();
    puts("Presenter tests passed: mapping, packing, bounds, retry, invalidation, capture.");
}
