idf_component_register(SRCS "storage.c" "touch.c" INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_sdspi esp_driver_i2c fatfs sdmmc)
