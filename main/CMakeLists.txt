idf_component_register(
    SRCS "main.cc" "zmk_scanner.cc" "cube_display.cc"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_lcd esp_timer
)
