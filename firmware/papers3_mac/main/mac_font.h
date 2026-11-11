#pragma once
#include <stdint.h>

/* A classic Macintosh bitmap strike (NFNT): every glyph lives side by side in
 * one wide bit image, `loc` gives each glyph's first column, and `ow` packs
 * the pen offset (high byte, relative to kern_max) with the advance width. */
typedef struct {
    uint16_t first, last, height, ascent, descent, leading;
    int16_t kern_max;
    uint16_t row_bytes;
    const uint8_t *bits;
    const uint16_t *loc, *ow;
} mac_font_t;
