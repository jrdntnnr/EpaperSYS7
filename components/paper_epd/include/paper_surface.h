#pragma once
#include "paper_presenter.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { PAPER_PHYSICAL_WIDTH = 960, PAPER_PHYSICAL_HEIGHT = 540,
       PAPER_SURFACE_BYTES = 960 * 540 / 4 };
/* Merge portrait rect into CW-rotated physical buffer; driver 0=white,3=black.
 * Reject malformed input without modifying destination. No retained pointers. */
bool paper_surface_merge(uint8_t *surface, paper_rect_t r, const uint8_t *data, size_t size);
#ifdef __cplusplus
}
#endif
