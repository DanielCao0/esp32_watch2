/**
 * File Browser LVGL Interface Implementation
 * Provides a graphical file browser for SD card content
 * Author: GitHub Copilot
 * Date: 2025-07-14
 */

#include "file_browser.h"
#include "sdcard.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "FILE_BROWSER";

// 文件浏览器组件
typedef struct {
    lv_obj_t* screen;           // 主屏幕
    lv_obj_t* header;           // 头部容器
    lv_obj_t* title_label;      // 标题标签
    lv_obj_t* path_label;       // 路径标签
    lv_obj_t* back_btn;         // 返回按钮
    lv_obj_t* up_btn;           // 上级目录按钮
    lv_obj_t* refresh_btn;      // 刷新按钮
    lv_obj_t* file_list;        // 文件列表
    lv_obj_t* status_bar;       // 状态栏
    lv_obj_t* status_label;     // 状态标签
    char current_path[512];     // 当前路径
} file_browser_t;

static file_browser_t browser = {0};

// 前向声明
static void back_btn_event_cb(lv_event_t* e);
static void up_btn_event_cb(lv_event_t* e);
static void refresh_btn_event_cb(lv_event_t* e);
static void file_item_event_cb(lv_event_t* e);
static void clean_file_list_memory(void);

/**
 * 根据文件扩展名判断文件类型
 */
static file_type_t get_file_type(const char* filename)
{
    if (filename == NULL) return FILE_TYPE_UNKNOWN;
    
    const char* ext = strrchr(filename, '.');
    if (ext == NULL) return FILE_TYPE_UNKNOWN;
    
    // 转换为小写进行比较
    char ext_lower[16];
    size_t ext_len = strlen(ext);
    if (ext_len >= sizeof(ext_lower)) return FILE_TYPE_UNKNOWN;
    
    for (size_t i = 0; i < ext_len; i++) {
        ext_lower[i] = tolower(ext[i]);
    }
    ext_lower[ext_len] = '\0';
    
    // 文本文件
    if (strcmp(ext_lower, ".txt") == 0 || strcmp(ext_lower, ".log") == 0 ||
        strcmp(ext_lower, ".md") == 0 || strcmp(ext_lower, ".json") == 0) {
        return FILE_TYPE_TEXT;
    }
    
    // 图片文件
    if (strcmp(ext_lower, ".jpg") == 0 || strcmp(ext_lower, ".jpeg") == 0 ||
        strcmp(ext_lower, ".png") == 0 || strcmp(ext_lower, ".bmp") == 0 ||
        strcmp(ext_lower, ".gif") == 0) {
        return FILE_TYPE_IMAGE;
    }
    
    // 音频文件
    if (strcmp(ext_lower, ".mp3") == 0 || strcmp(ext_lower, ".wav") == 0 ||
        strcmp(ext_lower, ".ogg") == 0 || strcmp(ext_lower, ".m4a") == 0) {
        return FILE_TYPE_AUDIO;
    }
    
    // 视频文件
    if (strcmp(ext_lower, ".mp4") == 0 || strcmp(ext_lower, ".avi") == 0 ||
        strcmp(ext_lower, ".mkv") == 0 || strcmp(ext_lower, ".mov") == 0) {
        return FILE_TYPE_VIDEO;
    }
    
    // 压缩文件
    if (strcmp(ext_lower, ".zip") == 0 || strcmp(ext_lower, ".rar") == 0 ||
        strcmp(ext_lower, ".7z") == 0 || strcmp(ext_lower, ".tar") == 0) {
        return FILE_TYPE_ARCHIVE;
    }
    
    return FILE_TYPE_UNKNOWN;
}

/**
 * 获取文件类型图标
 */
static const char* get_file_type_icon(file_type_t type, bool is_directory)
{
    if (is_directory) {
        return LV_SYMBOL_DIRECTORY;
    }
    
    switch (type) {
        case FILE_TYPE_TEXT:
            return LV_SYMBOL_FILE;
        case FILE_TYPE_IMAGE:
            return LV_SYMBOL_IMAGE;
        case FILE_TYPE_AUDIO:
            return LV_SYMBOL_AUDIO;
        case FILE_TYPE_VIDEO:
            return LV_SYMBOL_VIDEO;
        case FILE_TYPE_ARCHIVE:
            return LV_SYMBOL_DRIVE;  // 使用 DRIVE 图标代替不存在的 ARCHIVE
        default:
            return LV_SYMBOL_FILE;
    }
}

/**
 * 格式化文件大小
 */
static void format_file_size(size_t bytes, char* buffer, size_t buffer_size)
{
    if (bytes >= (1ULL << 30)) {
        snprintf(buffer, buffer_size, "%.1f GB", (double)bytes / (1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(buffer, buffer_size, "%.1f MB", (double)bytes / (1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(buffer, buffer_size, "%.1f KB", (double)bytes / (1ULL << 10));
    } else {
        snprintf(buffer, buffer_size, "%zu B", bytes);
    }
}

/**
 * 清理文件列表中分配的内存
 */
static void clean_file_list_memory(void)
{
    // 由于LVGL会在删除对象时自动清理事件，我们只需要确保
    // 在刷新时不会重复分配内存。这里暂时简化处理。
    ESP_LOGI(TAG, "Cleaning file list memory");
}

/**
 * 在文件列表容器中创建文件项按钮
 */
static lv_obj_t* create_file_item(lv_obj_t* parent, const char* icon, const char* text)
{
    // 创建按钮
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3c3c3c), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x5c5c5c), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_style_margin_all(btn, 0, 0);
    
    // 添加底部分隔线
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    
    // 创建水平布局容器
    lv_obj_t* content = lv_obj_create(btn);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    
    // 创建图标标签
    lv_obj_t* icon_label = lv_label_create(content);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_margin_right(icon_label, 8, 0);
    
    // 创建文本标签
    lv_obj_t* text_label = lv_label_create(content);
    lv_label_set_text(text_label, text);
    lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(text_label, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(text_label, LV_PCT(80));
    
    return btn;
}

/**
 * 设置文件项的颜色
 */
static void set_file_item_color(lv_obj_t* btn, lv_color_t color)
{
    // 查找按钮内的标签并设置颜色
    lv_obj_t* content = lv_obj_get_child(btn, 0);
    if (content != NULL) {
        lv_obj_t* icon_label = lv_obj_get_child(content, 0);
        lv_obj_t* text_label = lv_obj_get_child(content, 1);
        
        if (icon_label != NULL) {
            lv_obj_set_style_text_color(icon_label, color, 0);
        }
        if (text_label != NULL) {
            lv_obj_set_style_text_color(text_label, color, 0);
        }
    }
}

/**
 * 创建头部区域
 */
static void create_header(lv_obj_t* parent)
{
    // 头部容器
    browser.header = lv_obj_create(parent);
    lv_obj_set_size(browser.header, LV_PCT(100), 80);
    lv_obj_align(browser.header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(browser.header, lv_color_hex(0x2c3e50), 0);
    lv_obj_set_style_border_width(browser.header, 0, 0);
    lv_obj_set_style_radius(browser.header, 0, 0);
    lv_obj_set_style_pad_all(browser.header, 8, 0);
    
    // 返回按钮
    browser.back_btn = lv_btn_create(browser.header);
    lv_obj_set_size(browser.back_btn, 60, 50);
    lv_obj_align(browser.back_btn, LV_ALIGN_LEFT_MID, 0, 5);
    lv_obj_add_event_cb(browser.back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* back_label = lv_label_create(browser.back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    
    // 标题
    browser.title_label = lv_label_create(browser.header);
    lv_label_set_text(browser.title_label, "File Browser");
    lv_obj_set_style_text_color(browser.title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(browser.title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(browser.title_label, LV_ALIGN_TOP_MID, 0, 5);
    
    // 路径标签
    browser.path_label = lv_label_create(browser.header);
    lv_label_set_text(browser.path_label, "/sdcard");
    lv_obj_set_style_text_color(browser.path_label, lv_color_hex(0xbdc3c7), 0);
    lv_obj_set_style_text_font(browser.path_label, &lv_font_montserrat_16, 0);
    lv_obj_align(browser.path_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    // 上级目录按钮
    browser.up_btn = lv_btn_create(browser.header);
    lv_obj_set_size(browser.up_btn, 60, 50);
    lv_obj_align(browser.up_btn, LV_ALIGN_RIGHT_MID, 0, 5);
    lv_obj_add_event_cb(browser.up_btn, up_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* up_label = lv_label_create(browser.up_btn);
    lv_label_set_text(up_label, LV_SYMBOL_UP);
    lv_obj_center(up_label);
    
    // 刷新按钮
    // browser.refresh_btn = lv_btn_create(browser.header);
    // lv_obj_set_size(browser.refresh_btn, 40, 30);
    // lv_obj_align(browser.refresh_btn, LV_ALIGN_RIGHT_MID, 0, 5);
    // lv_obj_add_event_cb(browser.refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* refresh_label = lv_label_create(browser.refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_label);
}

/**
 * 创建状态栏
 */
static void create_status_bar(lv_obj_t* parent)
{
    browser.status_bar = lv_obj_create(parent);
    lv_obj_set_size(browser.status_bar, LV_PCT(100), 30);
    lv_obj_align(browser.status_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(browser.status_bar, lv_color_hex(0x34495e), 0);
    lv_obj_set_style_border_width(browser.status_bar, 0, 0);
    lv_obj_set_style_radius(browser.status_bar, 0, 0);
    lv_obj_set_style_pad_all(browser.status_bar, 5, 0);
    
    browser.status_label = lv_label_create(browser.status_bar);
    lv_label_set_text(browser.status_label, "Ready");
    lv_obj_set_style_text_color(browser.status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(browser.status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(browser.status_label, LV_ALIGN_CENTER, 0, 0);
}

/**
 * 更新状态栏信息
 */
static void update_status(const char* message)
{
    if (browser.status_label != NULL && message != NULL) {
        lv_label_set_text(browser.status_label, message);
    }
}

/**
 * 返回按钮事件处理
 */
static void back_btn_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Back button clicked");
        
        // 返回主屏幕
        if (ui_Screen1 != NULL) {
            lv_screen_load(ui_Screen1);
        }
    }
}

/**
 * 上级目录按钮事件处理
 */
static void up_btn_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Up directory button clicked, current path: %s", browser.current_path);
        
        // 检查是否已经在SD卡根目录
        if (strcmp(browser.current_path, SD_MOUNT_POINT) == 0) {
            update_status("Already at root directory");
            return;
        }
        
        // 获取上级目录
        char parent_path[512];
        strncpy(parent_path, browser.current_path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
        
        char* last_slash = strrchr(parent_path, '/');
        if (last_slash != NULL) {
            // 如果找到的斜杠是SD卡挂载点的一部分，则返回到根目录
            if (last_slash <= parent_path + strlen(SD_MOUNT_POINT) - 1) {
                strcpy(parent_path, SD_MOUNT_POINT);
            } else {
                *last_slash = '\0';  // 截断到上级目录
            }
            
            ESP_LOGI(TAG, "Going to parent directory: %s", parent_path);
            file_browser_refresh(parent_path);
        } else {
            // 没有找到斜杠，返回到根目录
            ESP_LOGI(TAG, "No slash found, returning to root");
            file_browser_refresh(SD_MOUNT_POINT);
        }
    }
}

/**
 * 刷新按钮事件处理
 */
static void refresh_btn_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Refresh button clicked");
        file_browser_refresh(browser.current_path);
    }
}

/**
 * 文件项目点击事件处理
 */
static void file_item_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t* btn = lv_event_get_target(e);
        file_info_t* file_info = (file_info_t*)lv_event_get_user_data(e);
        
        if (file_info != NULL) {
            ESP_LOGI(TAG, "File item clicked: %s", file_info->name);
            
            if (file_info->is_directory) {
                // 进入子目录
                file_browser_refresh(file_info->full_path);
            } else {
                // 文件操作（目前只显示信息）
                char status_msg[256];
                char size_str[32];
                format_file_size(file_info->size, size_str, sizeof(size_str));
                snprintf(status_msg, sizeof(status_msg), "File: %s (%s)", file_info->name, size_str);
                update_status(status_msg);
            }
        }
    }
}

/**
 * 创建文件浏览器屏幕
 */
lv_obj_t* file_browser_create(void)
{
    ESP_LOGI(TAG, "Creating file browser screen");
    
    // 创建独立屏幕
    browser.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(browser.screen, lv_color_hex(0x1a1a1a), 0);
    
    
    // 创建头部
    create_header(browser.screen);
    
    // 创建文件列表容器（可滚动）
    browser.file_list = lv_obj_create(browser.screen);
    lv_obj_set_size(browser.file_list, LV_PCT(100), LV_PCT(100) - 110);
    lv_obj_align(browser.file_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(browser.file_list, lv_color_hex(0x2c2c2c), 0);
    lv_obj_set_style_border_width(browser.file_list, 0, 0);
    lv_obj_set_style_radius(browser.file_list, 0, 0);
    lv_obj_set_style_pad_all(browser.file_list, 8, 0);
    
    // 启用垂直滚动
    lv_obj_set_scroll_dir(browser.file_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(browser.file_list, LV_SCROLLBAR_MODE_AUTO);
    
    // 设置文件列表为垂直布局
    lv_obj_set_flex_flow(browser.file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(browser.file_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(browser.file_list, 0, 0);
    
    // 设置文件列表的全局字体大小
    lv_obj_set_style_text_font(browser.file_list, &lv_font_montserrat_16, 0);
    
    // 禁用主屏幕的滚动
    lv_obj_clear_flag(browser.screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // 创建状态栏
    create_status_bar(browser.screen);
    
    // 设置初始路径
    strcpy(browser.current_path, SD_MOUNT_POINT);
    
    // 立即刷新文件列表以显示内容
    ESP_LOGI(TAG, "Attempting initial file list refresh...");
    esp_err_t refresh_result = file_browser_refresh(browser.current_path);
    if (refresh_result != ESP_OK) {
        ESP_LOGW(TAG, "Initial refresh failed, showing placeholder content");
        // 显示占位符内容
        lv_obj_t* placeholder_btn = create_file_item(browser.file_list, LV_SYMBOL_WARNING, "No SD Card or Error");
        set_file_item_color(placeholder_btn, lv_color_hex(0xe74c3c));
    }
    
    ESP_LOGI(TAG, "File browser screen created successfully");
    return browser.screen;
}

/**
 * 刷新文件列表
 */
esp_err_t file_browser_refresh(const char* path)
{
    if (path == NULL || browser.file_list == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for refresh");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Refreshing file list for path: %s", path);
    
    // 验证路径不为空
    if (strlen(path) == 0) {
        ESP_LOGW(TAG, "Empty path provided, using SD mount point");
        path = SD_MOUNT_POINT;
    }
    
    // 检查SD卡是否挂载
    if (!sdcard_is_mounted()) {
        update_status("SD card not mounted");
        ESP_LOGW(TAG, "SD card not mounted, showing example content");
        
        // 清空现有列表
        clean_file_list_memory();
        lv_obj_clean(browser.file_list);
        
        // 显示示例内容以便用户知道界面是工作的
        lv_obj_t* error_btn = create_file_item(browser.file_list, LV_SYMBOL_WARNING, "SD Card Not Found");
        set_file_item_color(error_btn, lv_color_hex(0xe74c3c));
        
        lv_obj_t* info_btn = create_file_item(browser.file_list, LV_SYMBOL_FILE, "Insert SD card to browse files");
        set_file_item_color(info_btn, lv_color_hex(0x95a5a6));
        
        lv_obj_t* example_btn = create_file_item(browser.file_list, LV_SYMBOL_DIRECTORY, "Example Folder");
        set_file_item_color(example_btn, lv_color_hex(0x3498db));
        
        lv_obj_t* example_file = create_file_item(browser.file_list, LV_SYMBOL_FILE, "example.txt");
        set_file_item_color(example_file, lv_color_white());
        
        // 更新路径标签
        lv_label_set_text(browser.path_label, "No SD Card");
        
        return ESP_ERR_INVALID_STATE;
    }
    
    // 清空现有列表
    clean_file_list_memory();
    lv_obj_clean(browser.file_list);
    
    // 更新当前路径
    strncpy(browser.current_path, path, sizeof(browser.current_path) - 1);
    browser.current_path[sizeof(browser.current_path) - 1] = '\0';
    
    // 更新路径标签
    lv_label_set_text(browser.path_label, browser.current_path);
    
    // 打开目录
    DIR* dir = opendir(path);
    if (dir == NULL) {
        update_status("Failed to open directory");
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        
        // 显示错误信息到文件列表
        clean_file_list_memory();
        lv_obj_clean(browser.file_list);
        lv_obj_t* error_btn = create_file_item(browser.file_list, LV_SYMBOL_WARNING, "Cannot open directory");
        set_file_item_color(error_btn, lv_color_hex(0xe74c3c));
        
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Directory opened successfully, reading contents...");
    
    struct dirent* entry;
    int file_count = 0;
    int dir_count = 0;
    
    // 读取目录内容
    while ((entry = readdir(dir)) != NULL) {
        // 跳过隐藏文件
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // 构建完整路径
        char full_path[512];
        // 确保路径正确构建，避免双斜杠
        if (path[strlen(path) - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", path, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        }
        
        // 获取文件信息
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            continue;
        }
        
        // 创建文件信息结构
        file_info_t* file_info = malloc(sizeof(file_info_t));
        if (file_info == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for file info");
            continue;
        }
        
        strncpy(file_info->name, entry->d_name, sizeof(file_info->name) - 1);
        file_info->name[sizeof(file_info->name) - 1] = '\0';
        
        strncpy(file_info->full_path, full_path, sizeof(file_info->full_path) - 1);
        file_info->full_path[sizeof(file_info->full_path) - 1] = '\0';
        
        file_info->is_directory = S_ISDIR(file_stat.st_mode);
        file_info->size = file_stat.st_size;
        file_info->type = get_file_type(entry->d_name);
        
        // 创建文件项
        lv_obj_t* btn = create_file_item(browser.file_list, 
                                        get_file_type_icon(file_info->type, file_info->is_directory), 
                                        file_info->name);
        
        // 为目录设置不同颜色
        if (file_info->is_directory) {
            set_file_item_color(btn, lv_color_hex(0x3498db));
            dir_count++;
        } else {
            file_count++;
        }
        
        // 添加点击事件
        lv_obj_add_event_cb(btn, file_item_event_cb, LV_EVENT_CLICKED, file_info);
    }
    
    closedir(dir);
    
    // 如果没有找到任何文件，显示提示信息
    if (file_count == 0 && dir_count == 0) {
        lv_obj_t* empty_btn = create_file_item(browser.file_list, LV_SYMBOL_FILE, "Directory is empty");
        set_file_item_color(empty_btn, lv_color_hex(0x95a5a6));
    }
    
    // 更新状态信息
    char status_msg[128];
    snprintf(status_msg, sizeof(status_msg), "%d folders, %d files", dir_count, file_count);
    update_status(status_msg);
    
    ESP_LOGI(TAG, "File list refreshed: %d directories, %d files", dir_count, file_count);
    return ESP_OK;
}

/**
 * 获取文件浏览器屏幕对象
 */
lv_obj_t* get_file_browser_screen(void)
{
    return browser.screen;
}

/**
 * 设置文件浏览器可见性
 */
void file_browser_set_visible(bool visible)
{
    if (browser.screen == NULL) {
        return;
    }
    
    if (visible) {
        // 只刷新文件列表，不重复加载屏幕
        file_browser_refresh(browser.current_path);
    } else {
        // 返回主屏幕
        if (ui_Screen1 != NULL) {
            lv_screen_load(ui_Screen1);
        }
    }
}
