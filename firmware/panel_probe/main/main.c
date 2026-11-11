#include "paper_epd.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>
void app_main(void)
{
    ESP_ERROR_CHECK(paper_epd_init());
    paper_presenter_t *p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    uint8_t *frame = heap_caps_calloc(1, PAPER_MAC_BYTES, MALLOC_CAP_SPIRAM);
    assert(p && frame);
    paper_presenter_init(p);
    /* Asymmetric native viewport: border, diagonal and top-left marker. */
    for (int y = 0; y < 342; ++y)
        for (int x = 0; x < 512; ++x)
            if (x == 0 || x == 511 || y == 0 || y == 341 || x == y || (x < 24 && y < 16))
                frame[y * 64 + x / 8] |= 0x80 >> (x % 8);
    while (!paper_presenter_present(p, frame, paper_epd_submit, NULL)) {
        paper_epd_flush_async();
        vTaskDelay(1);
    }
    ESP_ERROR_CHECK(paper_epd_wait_idle(30000));
    ESP_LOGI("probe", "Pattern painted; internal free=%u largest=%u PSRAM free=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    for (int seconds = 60; seconds > 0; --seconds) {
        ESP_LOGI("probe", "Portrait test visible: %d seconds until clean", seconds);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_ERROR_CHECK(paper_epd_full_clean(30000));
    ESP_LOGI("probe", "Panel cleaned. No automatic system power-off.");
    free(frame); free(p);
    for (;;) {
        ESP_LOGI("probe", "Probe complete; panel clean; min internal heap=%u",
            (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
