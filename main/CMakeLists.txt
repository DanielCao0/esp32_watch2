file(GLOB_RECURSE SRC_UI ui/*.c)

idf_component_register(
    SRCS "screen_power.c" "sdcard.c" "rtc.c" "hardware_rtc.c" "mpu6050.c" "example_qspi_with_ram.c" "wifi_connect.c" "clock.c" "lvgl_button.c"
        ${SRC_UI}
        "menu_screen.c"
    INCLUDE_DIRS "." "ui"
    REQUIRES lwip esp_netif esp_wifi driver esp_timer nvs_flash fatfs sdmmc
)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
