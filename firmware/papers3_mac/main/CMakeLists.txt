idf_component_register(SRCS "main.c" "ui.c" INCLUDE_DIRS "."
    REQUIRES macintosh paper_epd papers3_board esp_timer nvs_flash)
