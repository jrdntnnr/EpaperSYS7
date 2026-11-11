idf_component_register(SRCS "paper_surface.c" "paper_epd.cpp" INCLUDE_DIRS "include"
    REQUIRES paper_presenter epd_painter esp_timer)
