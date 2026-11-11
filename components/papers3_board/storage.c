#include "papers3_board.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"
static sdmmc_card_t *card;
esp_err_t papers3_storage_mount(void)
{
    if (card) return ESP_OK;
    const spi_bus_config_t bus = {.mosi_io_num=38, .miso_io_num=40,
        .sclk_io_num=39, .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=8192};
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = 47; slot.host_id = SPI2_HOST;
    const esp_vfs_fat_sdmmc_mount_config_t cfg = {.format_if_mount_failed=false,
        .max_files=8, .allocation_unit_size=16384};
    err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &cfg, &card);
    if (err != ESP_OK) { card=NULL; spi_bus_free(SPI2_HOST); }
    else ESP_LOGI("sd", "Mounted %s, %llu MB", card->cid.name,
        (unsigned long long)card->csd.capacity * card->csd.sector_size / 1048576);
    return err;
}
void papers3_storage_unmount(void)
{
    if (card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", card);
        card = NULL; spi_bus_free(SPI2_HOST);
    }
}
