/**
 * File Browser LVGL Interface
 * Provides a graphical file browser for SD card content
 * Author: GitHub Copilot
 * Date: 2025-07-14
 */

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 文件类型枚举
 */
typedef enum {
    FILE_TYPE_UNKNOWN = 0,
    FILE_TYPE_DIRECTORY,
    FILE_TYPE_TEXT,
    FILE_TYPE_IMAGE,
    FILE_TYPE_AUDIO,
    FILE_TYPE_VIDEO,
    FILE_TYPE_ARCHIVE
} file_type_t;

/**
 * 文件信息结构
 */
typedef struct {
    char name[256];           // 文件名
    char full_path[512];      // 完整路径
    file_type_t type;         // 文件类型
    size_t size;              // 文件大小
    bool is_directory;        // 是否为目录
} file_info_t;

/**
 * 创建文件浏览器屏幕
 * @return 文件浏览器屏幕对象，失败返回NULL
 */
lv_obj_t* file_browser_create(void);

/**
 * 刷新文件列表
 * @param path 要浏览的路径
 * @return ESP_OK成功，其他值失败
 */
esp_err_t file_browser_refresh(const char* path);

/**
 * 获取文件浏览器屏幕对象
 * @return 文件浏览器屏幕对象
 */
lv_obj_t* get_file_browser_screen(void);

/**
 * 设置文件浏览器可见性
 * @param visible 是否可见
 */
void file_browser_set_visible(bool visible);

#ifdef __cplusplus
}
#endif

#endif // FILE_BROWSER_H
