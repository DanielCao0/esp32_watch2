file(GLOB_RECURSE SRC_UI ui/*.c)

idf_component_register(
    SRCS "mpu6050.c" "example_qspi_with_ram.c" "wifi_connect.c" "clock.c" "lvgl_button.c" "wifi_stability_test.c" "button_test.c"
        ${SRC_UI}
        "menu_screen.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES lwip esp_netif esp_wifi driver esp_timer nvs_flash
)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
