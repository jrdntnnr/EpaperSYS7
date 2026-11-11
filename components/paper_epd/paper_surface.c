#include "paper_surface.h"
bool paper_surface_merge(uint8_t *surface, paper_rect_t r, const uint8_t *data, size_t size)
{
    if (!surface || !data || !r.width || !r.height || r.width % 4 ||
        r.x + r.width > PAPER_PANEL_WIDTH || r.y + r.height > PAPER_PANEL_HEIGHT ||
        size != (size_t)r.width * r.height / 4) return false;
    for (unsigned y = 0; y < r.height; ++y) {
        for (unsigned x = 0; x < r.width; ++x) {
            unsigned shade = (data[y * (r.width / 4) + x / 4] >> (6 - 2 * (x % 4))) & 3;
            // Match EPD_Painter ROTATION_CW: physical (y, 539-x).
            unsigned px = r.y + y, py = 539 - (r.x + x);
            unsigned shift = 6 - 2 * (px % 4);
            size_t index = py * 240 + px / 4;
            surface[index] = (surface[index] & ~(3u << shift)) | ((3u - shade) << shift);
        }
    }
    return true;
}
