#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint16_t x, y; uint8_t contacts; bool cancelled; } papers3_touch_t;
esp_err_t papers3_storage_mount(void);
void papers3_storage_unmount(void);
esp_err_t papers3_touch_init(void);
/* True only for a new report. Zero contacts is a release. Raw coordinates are
 * portrait 540x960, as documented by the official M5GFX PaperS3 configuration. */
bool papers3_touch_read(papers3_touch_t *event);
