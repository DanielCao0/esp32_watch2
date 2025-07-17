#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>

// 支持的图片格式
typedef enum {
    IMAGE_FORMAT_UNKNOWN,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPG,
    IMAGE_FORMAT_BMP,
    IMAGE_FORMAT_GIF
} image_format_t;

// 图片文件信息结构
typedef struct {
    char filename[128];
    char filepath[256];
    size_t file_size;
    image_format_t format;
} image_file_t;

// 图片浏览器控制结构
typedef struct {
    image_file_t* image_list;
    int image_count;
    int current_index;
    bool show_thumbnails;
    float zoom_level;
} image_viewer_t;

// 外部函数声明
lv_obj_t* image_viewer_create(void);
esp_err_t image_viewer_scan_files(void);
esp_err_t image_viewer_show_image(int index);
esp_err_t image_viewer_next_image(void);
esp_err_t image_viewer_previous_image(void);
esp_err_t image_viewer_zoom_in(void);
esp_err_t image_viewer_zoom_out(void);
esp_err_t image_viewer_reset_zoom(void);
void image_viewer_set_visible(bool visible);
lv_obj_t* get_image_viewer_screen(void);

#endif // IMAGE_VIEWER_H
