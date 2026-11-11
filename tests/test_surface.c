#include "paper_surface.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t surface[PAPER_SURFACE_BYTES];
static uint8_t reference[540 * 960];
static uint8_t packed[512 * 8 / 4];
static void check(void)
{
    /* Inverse transform, independent traversal of physical panel. */
    for (int py = 0; py < 540; ++py)
        for (int px = 0; px < 960; ++px) {
            unsigned actual = (surface[py * 240 + px / 4] >> (6 - 2 * (px % 4))) & 3;
            assert(actual == reference[px * 540 + (539 - py)]);
        }
}
static void merge(paper_rect_t r, unsigned seed)
{
    size_t size = r.width * r.height / 4;
    for (size_t i = 0; i < size; ++i) packed[i] = (uint8_t)(i * 37 + seed);
    assert(paper_surface_merge(surface, r, packed, size));
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            unsigned shade = (packed[y * (r.width / 4) + x / 4] >> (6 - 2 * (x % 4))) & 3;
            reference[(y + r.y) * 540 + x + r.x] = 3 - shade;
        }
    check();
}
int main(void)
{
    merge((paper_rect_t){14, 0, 512, 8}, 1);
    merge((paper_rect_t){14, 336, 512, 6}, 2);
    merge((paper_rect_t){0, 952, 4, 8}, 3);
    merge((paper_rect_t){536, 952, 4, 8}, 4);
    for (int i = 0; i < 100; ++i)
        merge((paper_rect_t){(uint16_t)(i * 17 % 532), (uint16_t)(i * 31 % 952), 8, 8}, i);
    uint8_t before[PAPER_SURFACE_BYTES];
    memcpy(before, surface, sizeof(surface));
    assert(!paper_surface_merge(surface, (paper_rect_t){539, 0, 4, 1}, packed, 1));
    assert(!paper_surface_merge(surface, (paper_rect_t){0, 960, 4, 1}, packed, 1));
    assert(!paper_surface_merge(surface, (paper_rect_t){0, 0, 3, 1}, packed, 1));
    assert(!paper_surface_merge(surface, (paper_rect_t){0, 0, 4, 1}, packed, 2));
    assert(!paper_surface_merge(surface, (paper_rect_t){0, 0, 0, 1}, packed, 0));
    assert(memcmp(before, surface, sizeof(surface)) == 0);
    puts("Surface tests passed: rotation, polarity, all edges, shared bytes, invalid input.");
}
