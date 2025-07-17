#include "image_viewer.h"
#include "sdcard.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

static const char *TAG = "IMAGE_VIEWER";

// 全局图片浏览器实例
static image_viewer_t g_image_viewer = {0};
static lv_obj_t *image_screen = NULL;

// UI 组件
static lv_obj_t *header_label = NULL;
static lv_obj_t *image_container = NULL;
static lv_obj_t *main_image = NULL;
static lv_obj_t *info_label = NULL;
static lv_obj_t *prev_btn = NULL;
static lv_obj_t *next_btn = NULL;

// 按钮事件回调
static void prev_btn_event_cb(lv_event_t *e);
static void next_btn_event_cb(lv_event_t *e);

// 工具函数
static bool is_image_file(const char *filename);
static image_format_t get_image_format(const char *filename);
static void update_ui_info(void);
static esp_err_t load_and_display_image(const char *filepath);

// 递归扫描目录的辅助函数
static int scan_directory_recursive(const char *dir_path, int *image_count, bool count_only);

esp_err_t image_viewer_scan_files(void)
{
    ESP_LOGI(TAG, "Scanning SD card for image files (with recursive search)...");
    
    // 释放之前的图片列表
    if (g_image_viewer.image_list) {
        heap_caps_free(g_image_viewer.image_list);
        g_image_viewer.image_list = NULL;
        g_image_viewer.image_count = 0;
    }
    
    // 检查SD卡挂载状态
    struct stat st;
    if (stat("/sdcard", &st) != 0) {
        ESP_LOGE(TAG, "SD card not mounted at /sdcard");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 第一次递归扫描：计算图片文件数量
    int image_count = 0;
    ESP_LOGI(TAG, "First pass: counting image files recursively...");
    int result = scan_directory_recursive("/sdcard", &image_count, true);
    if (result < 0) {
        ESP_LOGE(TAG, "Failed to scan SD card directories");
        return ESP_FAIL;
    }
    
    if (image_count == 0) {
        ESP_LOGW(TAG, "No image files found on SD card (searched recursively)");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Found %d image files total, allocating memory...", image_count);
    
    // 使用PSRAM分配图片列表内存
    g_image_viewer.image_list = heap_caps_malloc(image_count * sizeof(image_file_t), 
                                                 MALLOC_CAP_SPIRAM);
    if (!g_image_viewer.image_list) {
        ESP_LOGE(TAG, "Failed to allocate image list memory (%d bytes)", 
                 image_count * sizeof(image_file_t));
        return ESP_ERR_NO_MEM;
    }
    
    // 第二次递归扫描：填充图片列表
    g_image_viewer.image_count = 0;  // 用作索引计数器
    ESP_LOGI(TAG, "Second pass: loading image file information recursively...");
    result = scan_directory_recursive("/sdcard", &image_count, false);
    if (result < 0) {
        heap_caps_free(g_image_viewer.image_list);
        g_image_viewer.image_list = NULL;
        g_image_viewer.image_count = 0;
        ESP_LOGE(TAG, "Failed to load image files");
        return ESP_FAIL;
    }
    
    g_image_viewer.current_index = 0;
    g_image_viewer.zoom_level = 1.0f;
    
    ESP_LOGI(TAG, "Successfully loaded %d image files", g_image_viewer.image_count);
    
    update_ui_info();
    return ESP_OK;
}

// 递归扫描目录函数
static int scan_directory_recursive(const char *dir_path, int *image_count, bool count_only)
{
    ESP_LOGI(TAG, "Scanning directory: %s (count_only=%s)", dir_path, count_only ? "true" : "false");
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open directory: %s, error: %s", dir_path, strerror(errno));
        return 0;  // 不是致命错误，继续扫描其他目录
    }
    
    struct dirent *entry;
    int local_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 .. 目录
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 跳过其他隐藏文件和目录
        if (entry->d_name[0] == '.') {
            ESP_LOGD(TAG, "Skipping hidden file/dir: %s", entry->d_name);
            continue;
        }
        
        // 构建完整路径
        char full_path[512];
        int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (path_len >= sizeof(full_path)) {
            ESP_LOGW(TAG, "Path too long, skipping: %s/%s", dir_path, entry->d_name);
            continue;
        }
        
        // 使用 stat 检查文件类型
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            ESP_LOGW(TAG, "Failed to stat: %s, error: %s", full_path, strerror(errno));
            continue;
        }
        
        if (S_ISDIR(file_stat.st_mode)) {
            // 递归扫描子目录
            ESP_LOGI(TAG, "Found subdirectory: %s", full_path);
            int subdir_result = scan_directory_recursive(full_path, image_count, count_only);
            if (subdir_result < 0) {
                ESP_LOGW(TAG, "Error scanning subdirectory: %s", full_path);
            } else {
                local_count += subdir_result;
            }
        } else if (S_ISREG(file_stat.st_mode)) {
            // 检查是否为图片文件
            ESP_LOGI(TAG, "Found file: %s (size=%ld bytes)", full_path, file_stat.st_size);
            
            bool is_img = is_image_file(entry->d_name);
            ESP_LOGI(TAG, "Checking if '%s' is image file: %s", entry->d_name, is_img ? "YES" : "NO");
            
            if (is_img) {
                ESP_LOGI(TAG, "✓ Found image file: %s", full_path);
                
                if (count_only) {
                    (*image_count)++;
                    local_count++;
                } else {
                    // 填充图片信息
                    if (g_image_viewer.image_count < *image_count) {
                        image_file_t *img = &g_image_viewer.image_list[g_image_viewer.image_count];
                        
                        // 只保存文件名，不包含路径
                        strncpy(img->filename, entry->d_name, sizeof(img->filename) - 1);
                        img->filename[sizeof(img->filename) - 1] = '\0';
                        
                        // 保存完整路径
                        strncpy(img->filepath, full_path, sizeof(img->filepath) - 1);
                        img->filepath[sizeof(img->filepath) - 1] = '\0';
                        
                        img->file_size = file_stat.st_size;
                        img->format = get_image_format(entry->d_name);
                        
                        ESP_LOGI(TAG, "Added image %d: filename='%s', filepath='%s' (%zu bytes)", 
                                 g_image_viewer.image_count, img->filename, img->filepath, img->file_size);
                        ESP_LOGI(TAG, "  entry->d_name='%s', full_path='%s'", entry->d_name, full_path);
                        
                        g_image_viewer.image_count++;
                        local_count++;
                    }
                }
            }
        }
    }
    
    closedir(dir);
    ESP_LOGI(TAG, "Directory %s scan complete, found %d images", dir_path, local_count);
    return local_count;
}

lv_obj_t* image_viewer_create(void)
{
    if (image_screen) {
        return image_screen;
    }
    
    // 创建主屏幕
    image_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(image_screen, lv_color_black(), 0);
    
    // 创建标题栏
    lv_obj_t *header = lv_obj_create(image_screen);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    
    header_label = lv_label_create(header);
    lv_label_set_text(header_label, "Image Viewer");
    lv_obj_set_style_text_color(header_label, lv_color_white(), 0);
    lv_obj_center(header_label);
    
    // 创建图片显示区域 - 扩大显示区域
    image_container = lv_obj_create(image_screen);
    lv_obj_set_size(image_container, LV_PCT(100), 380);  // 增加高度
    lv_obj_align(image_container, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(image_container, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(image_container, 1, 0);
    lv_obj_set_style_border_color(image_container, lv_color_hex(0x555555), 0);
    
    // 主图片显示
    main_image = lv_img_create(image_container);
    lv_obj_center(main_image);
    lv_obj_set_style_bg_color(main_image, lv_color_hex(0x222222), 0);
    
    // 信息标签
    info_label = lv_label_create(image_container);
    lv_label_set_text(info_label, "No images found");
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    // 控制按钮区域
    lv_obj_t *control_container = lv_obj_create(image_screen);
    lv_obj_set_size(control_container, LV_PCT(100), 50);
    lv_obj_align(control_container, LV_ALIGN_BOTTOM_MID, 0, 0);  // 移到底部
    lv_obj_set_style_bg_opa(control_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_container, 0, 0);
    
    // 上一张按钮
    prev_btn = lv_btn_create(control_container);
    lv_obj_set_size(prev_btn, 100, 40);  // 增大按钮
    lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_event_cb(prev_btn, prev_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_LEFT " Prev");
    lv_obj_center(prev_label);
    
    // 下一张按钮
    next_btn = lv_btn_create(control_container);
    lv_obj_set_size(next_btn, 100, 40);  // 增大按钮
    lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(next_btn, next_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, "Next " LV_SYMBOL_RIGHT);
    lv_obj_center(next_label);
    
    // 扫描图片文件
    image_viewer_scan_files();
    
    return image_screen;
}

esp_err_t image_viewer_show_image(int index)
{
    if (!g_image_viewer.image_list || index >= g_image_viewer.image_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_image_viewer.current_index = index;
    image_file_t *current = &g_image_viewer.image_list[index];
    
    ESP_LOGI(TAG, "Showing image: %s", current->filename);
    
    // 加载并显示图片
    esp_err_t ret = load_and_display_image(current->filepath);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load image: %s", current->filename);
        
        // 显示错误信息
        if (main_image) {
            lv_obj_clean(main_image);
            lv_obj_t *error_label = lv_label_create(main_image);
            lv_label_set_text_fmt(error_label, "Failed to load:\n%s\nFormat: %s\nSize: %zu bytes", 
                                 current->filename,
                                 (current->format == IMAGE_FORMAT_PNG) ? "PNG" :
                                 (current->format == IMAGE_FORMAT_JPG) ? "JPG" :
                                 (current->format == IMAGE_FORMAT_BMP) ? "BMP" :
                                 (current->format == IMAGE_FORMAT_GIF) ? "GIF" : "Unknown",
                                 current->file_size);
            lv_obj_set_style_text_color(error_label, lv_color_hex(0xff6666), 0);
            lv_obj_center(error_label);
        }
    }
    
    update_ui_info();
    
    return ESP_OK;
}

esp_err_t image_viewer_next_image(void)
{
    if (!g_image_viewer.image_list || g_image_viewer.image_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int next_index = (g_image_viewer.current_index + 1) % g_image_viewer.image_count;
    return image_viewer_show_image(next_index);
}

esp_err_t image_viewer_previous_image(void)
{
    if (!g_image_viewer.image_list || g_image_viewer.image_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int prev_index = g_image_viewer.current_index - 1;
    if (prev_index < 0) {
        prev_index = g_image_viewer.image_count - 1;
    }
    
    return image_viewer_show_image(prev_index);
}

void image_viewer_set_visible(bool visible)
{
    if (image_screen) {
        if (visible) {
            lv_scr_load(image_screen);
        } else {
            lv_obj_add_flag(image_screen, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_obj_t* get_image_viewer_screen(void)
{
    return image_screen;
}

// 私有函数实现
static bool is_image_file(const char *filename)
{
    if (!filename) {
        ESP_LOGD(TAG, "is_image_file: filename is NULL");
        return false;
    }
    
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        ESP_LOGD(TAG, "is_image_file: no extension found in '%s'", filename);
        return false;
    }
    
    ESP_LOGD(TAG, "is_image_file: checking '%s', extension='%s'", filename, ext);
    
    // 转换为小写进行比较，与file_browser.c保持一致
    char ext_lower[16];
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) {
        ESP_LOGD(TAG, "is_image_file: extension too long (%zu chars): %s", ext_len, ext);
        return false;
    }
    
    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower(ext[i]);
    }
    ext_lower[ext_len] = '\0';
    
    ESP_LOGD(TAG, "is_image_file: lowercase extension='%s'", ext_lower);
    
    bool result = (strcmp(ext_lower, ".png") == 0 ||
                   strcmp(ext_lower, ".jpg") == 0 ||
                   strcmp(ext_lower, ".jpeg") == 0 ||
                   strcmp(ext_lower, ".bmp") == 0 ||
                   strcmp(ext_lower, ".gif") == 0);
                   
    ESP_LOGD(TAG, "is_image_file: '%s' -> %s", filename, result ? "TRUE" : "FALSE");
    return result;
}

static image_format_t get_image_format(const char *filename)
{
    if (!filename) return IMAGE_FORMAT_UNKNOWN;
    
    const char *ext = strrchr(filename, '.');
    if (!ext) return IMAGE_FORMAT_UNKNOWN;
    
    // 转换为小写进行比较，与file_browser.c和is_image_file保持一致
    char ext_lower[16];
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) return IMAGE_FORMAT_UNKNOWN;
    
    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower(ext[i]);
    }
    ext_lower[ext_len] = '\0';
    
    if (strcmp(ext_lower, ".png") == 0) {
        return IMAGE_FORMAT_PNG;
    } else if (strcmp(ext_lower, ".jpg") == 0 || 
               strcmp(ext_lower, ".jpeg") == 0) {
        return IMAGE_FORMAT_JPG;
    } else if (strcmp(ext_lower, ".bmp") == 0) {
        return IMAGE_FORMAT_BMP;
    } else if (strcmp(ext_lower, ".gif") == 0) {
        return IMAGE_FORMAT_GIF;
    }
    
    return IMAGE_FORMAT_UNKNOWN;
}

static void update_ui_info(void)
{
    if (!g_image_viewer.image_list || g_image_viewer.image_count == 0) {
        if (info_label) {
            lv_label_set_text(info_label, "No images found on SD card");
        }
        if (header_label) {
            lv_label_set_text(header_label, "Image Viewer - No Images");
        }
        return;
    }
    
    image_file_t *current = &g_image_viewer.image_list[g_image_viewer.current_index];
    
    if (info_label) {
        lv_label_set_text_fmt(info_label, "%s (%d/%d)", 
                             current->filename,
                             g_image_viewer.current_index + 1,
                             g_image_viewer.image_count);
    }
    
    if (header_label) {
        lv_label_set_text_fmt(header_label, "Image Viewer (%d/%d)", 
                             g_image_viewer.current_index + 1,
                             g_image_viewer.image_count);
    }
}

// 事件回调函数
static void prev_btn_event_cb(lv_event_t *e)
{
    image_viewer_previous_image();
}

static void next_btn_event_cb(lv_event_t *e)
{
    image_viewer_next_image();
}

// 图片加载和显示函数
static esp_err_t load_and_display_image(const char *filepath)
{
    ESP_LOGI(TAG, "Loading image from: %s", filepath);
    
    if (!main_image) {
        ESP_LOGE(TAG, "Main image object is NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查文件是否存在
    struct stat file_stat;
    if (stat(filepath, &file_stat) != 0) {
        ESP_LOGE(TAG, "File does not exist: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!S_ISREG(file_stat.st_mode)) {
        ESP_LOGE(TAG, "Not a regular file: %s", filepath);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "File size: %ld bytes", file_stat.st_size);
    
    ESP_LOGI(TAG, "Converting path: original='%s'", filepath);
    
    // 将路径转换为 LVGL 的 A: 盘路径 - 只是在前面加上 A:
    char lvgl_path[256];
    snprintf(lvgl_path, sizeof(lvgl_path), "A:%s", filepath);
    ESP_LOGI(TAG, "Path conversion: '%s' -> '%s'", filepath, lvgl_path);
    
    // 清理之前的内容
    lv_obj_clean(main_image);
    
    // 创建新的图片对象
    lv_obj_t *img_obj = lv_img_create(main_image);
    if (!img_obj) {
        ESP_LOGE(TAG, "Failed to create image object");
        return ESP_ERR_NO_MEM;
    }
    
    // 使用 LVGL 的 A: 盘路径加载图片
    ESP_LOGI(TAG, "Setting image source to: %s", lvgl_path);
    lv_img_set_src(img_obj, lvgl_path);
    
    // 居中显示，使用原始像素大小
    lv_obj_center(img_obj);
    
    // 设置图片样式
    lv_obj_set_style_bg_opa(img_obj, LV_OPA_TRANSP, 0);
    
    // 检查图片是否加载成功
    const void *src = lv_img_get_src(img_obj);
    if (src == NULL) {
        ESP_LOGW(TAG, "Image source is NULL, may not be loaded properly: %s", lvgl_path);
        
        // 显示加载失败信息
        lv_obj_del(img_obj);
        lv_obj_t *error_label = lv_label_create(main_image);
        
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;
        
        lv_label_set_text_fmt(error_label, 
            "Cannot display image:\n%s\n\nLVGL Path: %s\nSize: %ld bytes\n\nPossible reasons:\n"
            "- Unsupported format\n- File corrupted\n- Memory insufficient\n- LVGL decoder disabled", 
            filename, lvgl_path, file_stat.st_size);
            
        lv_obj_set_style_text_color(error_label, lv_color_hex(0xff9999), 0);
        lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(error_label);
        
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGI(TAG, "Image loaded successfully from: %s", filepath);
    return ESP_OK;
}
