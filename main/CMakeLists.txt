file(GLOB_RECURSE SRC_UI ui/*.c)

idf_component_register(
    SRCS "mpu6050.c" "example_qspi_with_ram.c"
        ${SRC_UI}
        "menu_screen.c"
    INCLUDE_DIRS "." "ui"
)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
