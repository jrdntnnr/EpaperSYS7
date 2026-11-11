set(epd "${CMAKE_CURRENT_LIST_DIR}/../../vendor/EPD_Painter/src")
idf_component_register(SRCS "${epd}/EPD_Painter.cpp" "${epd}/EPD_Painter.S"
    "${epd}/epd_pin_driver.cpp" "${epd}/epd_painter_powerctl.cpp"
    "${epd}/epd_painter_bootctl.cpp" "${epd}/epd_painter_decision.cpp"
    INCLUDE_DIRS "${epd}"
    REQUIRES esp_driver_gpio esp_timer esp_hw_support nvs_flash esp_pm)
target_compile_definitions(${COMPONENT_LIB} PUBLIC EPD_PAINTER_PRESET_M5PAPER_S3 EPD_PAINTER_NO_NVS)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
