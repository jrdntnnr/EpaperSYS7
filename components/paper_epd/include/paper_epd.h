#pragma once
#include "esp_err.h"
#include "paper_surface.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Process-lifetime singleton. Call once, on core 0, before network startup.
 * One producer task must own all public calls. Init failure requires reboot.
 * No APIs may be called from ISR. */
esp_err_t paper_epd_init(void);
/* Panel drive presets: passes and settle per paint, hence contrast vs latency. */
typedef enum { PAPER_SPEED_FAST, PAPER_SPEED_BALANCED, PAPER_SPEED_SHARP, PAPER_SPEEDS } paper_epd_speed_t;
/* Takes effect before the next paint; safe to call while painting. */
void paper_epd_set_speed(paper_epd_speed_t speed);
/* Presenter-compatible callback; zero-wait mutex. Copies into desired shadow.
 * Accepted rectangles coalesce there until flush. context is unused. */
bool paper_epd_submit(void *context, paper_rect_t rect, const uint8_t *data, size_t size);
void paper_epd_flush_async(void);
/* Wall time of the most recent physical paint, for throughput logging. */
uint32_t paper_epd_last_paint_ms(void);
/* Hard-clean controls on layout changes, then restore the latest whole shadow.
 * The guest viewport ends on a packed-byte boundary and is not touched. */
void paper_epd_clean_ui_async(void);
/* Flush and wait for all accepted state, bounded by timeout_ms. */
esp_err_t paper_epd_wait_idle(uint32_t timeout_ms);
/* Caller must stop producing. Clears glass after pending paint; does not cut
 * system power or flush disks. Presenter must be invalidated before reuse. */
esp_err_t paper_epd_full_clean(uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
