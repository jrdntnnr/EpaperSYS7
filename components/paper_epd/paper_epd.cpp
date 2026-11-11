#include "paper_epd.h"
#include "EPD_Painter.h"
#include "EPD_Painter_presets.h"
#include "esp_timer.h"
#include <cstring>

static EPD_Painter driver(EPD_M5PAPER_S3_PRESET);
static SemaphoreHandle_t mutex;
static TaskHandle_t worker_task;
static uint8_t *desired, *snapshot;
static uint64_t requested, completed;
static bool clean_requested, clean_ui_requested;
static bool initialized, attempted;
static uint32_t last_paint_ms;
static paper_epd_speed_t speed = PAPER_SPEED_BALANCED, applied_speed = PAPER_SPEEDS;

/* Coalesce bursts of submits; the paint itself is the real pacing. */
static const uint32_t COALESCE_MS = 5;
/* NORMAL settle gap: preset 4000us is calibrated for greys; B/W saturates sooner. */
static const int SETTLE_BALANCED_US = 1500, SETTLE_SHARP_US = 4000;

static void apply_speed(paper_epd_speed_t s)
{
    if (s == applied_speed) return;
    applied_speed = s;
    if (s == PAPER_SPEED_FAST) {
        driver.setQuality(EPD_Painter::Quality::QUALITY_FAST);
        return;
    }
    driver.setPassGapNormal(s == PAPER_SPEED_SHARP ? SETTLE_SHARP_US : SETTLE_BALANCED_US);
    driver.setQuality(EPD_Painter::Quality::QUALITY_NORMAL);
}

static void worker(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(COALESCE_MS));
        xSemaphoreTake(mutex, portMAX_DELAY);
        const uint64_t generation = requested;
        if (generation == completed) { xSemaphoreGive(mutex); continue; }
        const paper_epd_speed_t wanted = speed;
        const bool clean = clean_requested;
        clean_requested = false;
        const bool clean_ui = clean_ui_requested;
        clean_ui_requested = false;
        if (clean) memset(desired, 0, PAPER_SURFACE_BYTES);
        memcpy(snapshot, desired, PAPER_SURFACE_BYTES);
        xSemaphoreGive(mutex);
        apply_speed(wanted);
        if (clean) driver.clear();
        else {
            if (clean_ui) {
                // Portrait y>=356 maps to physical x>=356, a packed-byte
                // boundary, so the guest viewport (y<356) is untouched.
                const EPD_Painter::Rect region = {PAPER_VIEW_Y + PAPER_MAC_HEIGHT, 0,
                                                  PAPER_PANEL_HEIGHT - PAPER_VIEW_Y - PAPER_MAC_HEIGHT, 540};
                driver.clear(&region, 1, EPD_Painter::ClearMode::HARD);
            }
            driver.paintPacked(snapshot);
        }
        const int64_t started = esp_timer_get_time();
        driver.waitUntilIdle();
        last_paint_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
        xSemaphoreTake(mutex, portMAX_DELAY);
        completed = generation;
        xSemaphoreGive(mutex);
    }
}
esp_err_t paper_epd_init(void)
{
    if (attempted) return ESP_ERR_INVALID_STATE;
    attempted = true;
    mutex = xSemaphoreCreateMutex();
    desired = static_cast<uint8_t *>(heap_caps_calloc(1, PAPER_SURFACE_BYTES, MALLOC_CAP_SPIRAM));
    snapshot = static_cast<uint8_t *>(heap_caps_calloc(1, PAPER_SURFACE_BYTES, MALLOC_CAP_SPIRAM));
    if (!mutex || !desired || !snapshot) return ESP_ERR_NO_MEM;
    driver.setAutoShutdown(false);
    if (!driver.begin()) return ESP_FAIL;
    driver.clearBuffers();
    driver.clear();
    apply_speed(speed);
    if (xTaskCreatePinnedToCore(worker, "paper_epd", 4096, nullptr, 5, &worker_task, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    initialized = true;
    return ESP_OK;
}
void paper_epd_set_speed(paper_epd_speed_t s)
{
    if (!initialized || s >= PAPER_SPEEDS) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    speed = s;
    ++requested;
    xSemaphoreGive(mutex);
    xTaskNotifyGive(worker_task);
}
uint32_t paper_epd_last_paint_ms(void)
{
    return last_paint_ms;
}
bool paper_epd_submit(void *, paper_rect_t r, const uint8_t *data, size_t size)
{
    if (!initialized || xSemaphoreTake(mutex, 0) != pdTRUE) return false;
    bool ok = paper_surface_merge(desired, r, data, size);
    xSemaphoreGive(mutex);
    return ok;
}
void paper_epd_flush_async(void)
{
    if (initialized) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        ++requested;
        xSemaphoreGive(mutex);
        xTaskNotifyGive(worker_task);
    }
}
void paper_epd_clean_ui_async(void)
{
    if (!initialized) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    clean_ui_requested = true;
    ++requested;
    xSemaphoreGive(mutex);
    xTaskNotifyGive(worker_task);
}
static esp_err_t wait_request(uint32_t timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    do {
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            bool done = completed == requested;
            xSemaphoreGive(mutex);
            if (done) return ESP_OK;
        }
        vTaskDelay(1);
    } while (esp_timer_get_time() < deadline);
    return ESP_ERR_TIMEOUT;
}
esp_err_t paper_epd_wait_idle(uint32_t timeout_ms)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    paper_epd_flush_async();
    return wait_request(timeout_ms);
}
esp_err_t paper_epd_full_clean(uint32_t timeout_ms)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    esp_err_t err = paper_epd_wait_idle(timeout_ms);
    if (err != ESP_OK) return err;
    xSemaphoreTake(mutex, portMAX_DELAY);
    clean_requested = true;
    ++requested;
    xSemaphoreGive(mutex);
    xTaskNotifyGive(worker_task);
    return wait_request(timeout_ms);
}
