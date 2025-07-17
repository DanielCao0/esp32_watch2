#include "music_player.h"
#include "sdcard.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "MUSIC_PLAYER";

// 全局播放器实例
static music_player_t g_music_player = {0};
static lv_obj_t *music_screen = NULL;

// UI 组件
static lv_obj_t *song_title_label = NULL;
static lv_obj_t *artist_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *current_time_label = NULL;
static lv_obj_t *total_time_label = NULL;
static lv_obj_t *playlist_list = NULL;
static lv_obj_t *play_btn = NULL;
static lv_obj_t *prev_btn = NULL;
static lv_obj_t *next_btn = NULL;

// 按钮事件回调
static void play_btn_event_cb(lv_event_t *e);
static void prev_btn_event_cb(lv_event_t *e);
static void next_btn_event_cb(lv_event_t *e);
static void playlist_item_event_cb(lv_event_t *e);

// 工具函数
static bool is_mp3_file(const char *filename);
static void update_ui_info(void);
static void format_time(uint32_t seconds, char *buffer);

esp_err_t music_player_scan_files(void)
{
    ESP_LOGI(TAG, "Scanning SD card for MP3 files...");
    
    // 释放之前的播放列表
    if (g_music_player.playlist) {
        heap_caps_free(g_music_player.playlist);
        g_music_player.playlist = NULL;
        g_music_player.playlist_count = 0;
    }
    
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open SD card directory");
        return ESP_FAIL;
    }
    
    // 第一次遍历：计算MP3文件数量
    struct dirent *entry;
    int mp3_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (is_mp3_file(entry->d_name)) {
            mp3_count++;
        }
    }
    rewinddir(dir);
    
    if (mp3_count == 0) {
        ESP_LOGW(TAG, "No MP3 files found");
        closedir(dir);
        return ESP_OK;
    }
    
    // 使用PSRAM分配播放列表内存
    g_music_player.playlist = heap_caps_malloc(mp3_count * sizeof(music_file_t), 
                                               MALLOC_CAP_SPIRAM);
    if (!g_music_player.playlist) {
        ESP_LOGE(TAG, "Failed to allocate playlist memory");
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    
    // 第二次遍历：填充播放列表
    int index = 0;
    while ((entry = readdir(dir)) != NULL && index < mp3_count) {
        if (is_mp3_file(entry->d_name)) {
            music_file_t *file = &g_music_player.playlist[index];
            
            // 填充文件信息
            strncpy(file->filename, entry->d_name, sizeof(file->filename) - 1);
            snprintf(file->filepath, sizeof(file->filepath), "/sdcard/%s", entry->d_name);
            
            // 提取标题（去掉.mp3扩展名）
            strncpy(file->title, entry->d_name, sizeof(file->title) - 1);
            char *dot = strrchr(file->title, '.');
            if (dot) *dot = '\0';
            
            // 默认艺术家信息
            strcpy(file->artist, "Unknown Artist");
            
            // 获取文件大小
            struct stat file_stat;
            if (stat(file->filepath, &file_stat) == 0) {
                file->file_size = file_stat.st_size;
            } else {
                file->file_size = 0;
            }
            
            // 估算时长（假设128kbps）
            file->duration = file->file_size / (128 * 1000 / 8);
            
            index++;
        }
    }
    
    closedir(dir);
    g_music_player.playlist_count = index;
    g_music_player.current_index = 0;
    g_music_player.state = PLAYER_STATE_STOPPED;
    
    ESP_LOGI(TAG, "Found %d MP3 files", g_music_player.playlist_count);
    
    // 更新播放列表UI
    if (playlist_list) {
        lv_obj_clean(playlist_list);
        for (int i = 0; i < g_music_player.playlist_count; i++) {
            lv_obj_t *btn = lv_list_add_btn(playlist_list, LV_SYMBOL_AUDIO, 
                                           g_music_player.playlist[i].title);
            lv_obj_add_event_cb(btn, playlist_item_event_cb, LV_EVENT_CLICKED, 
                               (void*)(intptr_t)i);
        }
    }
    
    update_ui_info();
    return ESP_OK;
}

lv_obj_t* music_player_create(void)
{
    if (music_screen) {
        return music_screen;
    }
    
    // 创建主屏幕
    music_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(music_screen, lv_color_black(), 0);
    
    // 创建标题栏
    lv_obj_t *header = lv_obj_create(music_screen);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    
    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, "Music Player");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_center(title_label);
    
    // 创建歌曲信息区域
    lv_obj_t *info_container = lv_obj_create(music_screen);
    lv_obj_set_size(info_container, LV_PCT(90), 120);
    lv_obj_align(info_container, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_width(info_container, 1, 0);
    lv_obj_set_style_border_color(info_container, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(info_container, 10, 0);
    
    // 歌曲标题
    song_title_label = lv_label_create(info_container);
    lv_label_set_text(song_title_label, "No Song Selected");
    lv_obj_set_style_text_color(song_title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(song_title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(song_title_label, LV_ALIGN_TOP_MID, 0, 10);
    
    // 艺术家
    artist_label = lv_label_create(info_container);
    lv_label_set_text(artist_label, "Unknown Artist");
    lv_obj_set_style_text_color(artist_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(artist_label, LV_ALIGN_TOP_MID, 0, 35);
    
    // 进度条
    progress_bar = lv_bar_create(info_container);
    lv_obj_set_size(progress_bar, LV_PCT(80), 6);
    lv_obj_align(progress_bar, LV_ALIGN_TOP_MID, 0, 65);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    
    // 时间标签
    current_time_label = lv_label_create(info_container);
    lv_label_set_text(current_time_label, "0:00");
    lv_obj_set_style_text_color(current_time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(current_time_label, &lv_font_montserrat_12, 0);
    lv_obj_align(current_time_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    
    total_time_label = lv_label_create(info_container);
    lv_label_set_text(total_time_label, "0:00");
    lv_obj_set_style_text_color(total_time_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(total_time_label, &lv_font_montserrat_12, 0);
    lv_obj_align(total_time_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    
    // 控制按钮区域
    lv_obj_t *control_container = lv_obj_create(music_screen);
    lv_obj_set_size(control_container, LV_PCT(90), 60);
    lv_obj_align(control_container, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_bg_opa(control_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(control_container, 0, 0);
    
    // 上一首按钮
    prev_btn = lv_btn_create(control_container);
    lv_obj_set_size(prev_btn, 50, 50);
    lv_obj_align(prev_btn, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_add_event_cb(prev_btn, prev_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_label = lv_label_create(prev_btn);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);
    lv_obj_center(prev_label);
    
    // 播放/暂停按钮
    play_btn = lv_btn_create(control_container);
    lv_obj_set_size(play_btn, 60, 60);
    lv_obj_align(play_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(play_btn, play_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *play_label = lv_label_create(play_btn);
    lv_label_set_text(play_label, LV_SYMBOL_PLAY);
    lv_obj_center(play_label);
    
    // 下一首按钮
    next_btn = lv_btn_create(control_container);
    lv_obj_set_size(next_btn, 50, 50);
    lv_obj_align(next_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(next_btn, next_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_label = lv_label_create(next_btn);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);
    lv_obj_center(next_label);
    
    // 播放列表
    playlist_list = lv_list_create(music_screen);
    lv_obj_set_size(playlist_list, LV_PCT(90), 180);
    lv_obj_align(playlist_list, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(playlist_list, lv_color_hex(0x111111), 0);
    
    // 禁用滚动和滚动条
    lv_obj_clear_flag(playlist_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(playlist_list, LV_SCROLLBAR_MODE_OFF);
    
    // 扫描MP3文件
    music_player_scan_files();
    
    return music_screen;
}

esp_err_t music_player_play(int index)
{
    if (!g_music_player.playlist || index >= g_music_player.playlist_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_music_player.current_index = index;
    g_music_player.state = PLAYER_STATE_PLAYING;
    g_music_player.current_position = 0;
    
    ESP_LOGI(TAG, "Playing: %s", g_music_player.playlist[index].title);
    
    // TODO: 实际播放函数，由用户实现
    // 例如：mp3_decoder_start(g_music_player.playlist[index].filepath);
    
    update_ui_info();
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        lv_label_set_text(label, LV_SYMBOL_PAUSE);
    }
    
    return ESP_OK;
}

esp_err_t music_player_pause(void)
{
    if (g_music_player.state != PLAYER_STATE_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_music_player.state = PLAYER_STATE_PAUSED;
    ESP_LOGI(TAG, "Music paused");
    
    // TODO: 实际暂停函数，由用户实现
    // 例如：mp3_decoder_pause();
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        lv_label_set_text(label, LV_SYMBOL_PLAY);
    }
    
    return ESP_OK;
}

esp_err_t music_player_resume(void)
{
    if (g_music_player.state != PLAYER_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_music_player.state = PLAYER_STATE_PLAYING;
    ESP_LOGI(TAG, "Music resumed");
    
    // TODO: 实际恢复播放函数，由用户实现
    // 例如：mp3_decoder_resume();
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        lv_label_set_text(label, LV_SYMBOL_PAUSE);
    }
    
    return ESP_OK;
}

esp_err_t music_player_stop(void)
{
    g_music_player.state = PLAYER_STATE_STOPPED;
    g_music_player.current_position = 0;
    ESP_LOGI(TAG, "Music stopped");
    
    // TODO: 实际停止函数，由用户实现
    // 例如：mp3_decoder_stop();
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        lv_label_set_text(label, LV_SYMBOL_PLAY);
    }
    
    // 重置进度条
    if (progress_bar) {
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    }
    
    if (current_time_label) {
        lv_label_set_text(current_time_label, "0:00");
    }
    
    return ESP_OK;
}

esp_err_t music_player_next(void)
{
    if (!g_music_player.playlist || g_music_player.playlist_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int next_index = (g_music_player.current_index + 1) % g_music_player.playlist_count;
    return music_player_play(next_index);
}

esp_err_t music_player_previous(void)
{
    if (!g_music_player.playlist || g_music_player.playlist_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int prev_index = g_music_player.current_index - 1;
    if (prev_index < 0) {
        prev_index = g_music_player.playlist_count - 1;
    }
    
    return music_player_play(prev_index);
}

void music_player_set_visible(bool visible)
{
    if (music_screen) {
        if (visible) {
            lv_scr_load(music_screen);
        } else {
            lv_obj_add_flag(music_screen, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_obj_t* get_music_player_screen(void)
{
    return music_screen;
}

// 私有函数实现
static bool is_mp3_file(const char *filename)
{
    if (!filename) return false;
    
    size_t len = strlen(filename);
    if (len < 4) return false;
    
    const char *ext = filename + len - 4;
    return (strcasecmp(ext, ".mp3") == 0);
}

static void update_ui_info(void)
{
    if (!g_music_player.playlist || g_music_player.playlist_count == 0) {
        if (song_title_label) {
            lv_label_set_text(song_title_label, "No Songs Found");
        }
        if (artist_label) {
            lv_label_set_text(artist_label, "Please add MP3 files to SD card");
        }
        return;
    }
    
    music_file_t *current = &g_music_player.playlist[g_music_player.current_index];
    
    if (song_title_label) {
        lv_label_set_text(song_title_label, current->title);
    }
    
    if (artist_label) {
        lv_label_set_text(artist_label, current->artist);
    }
    
    if (total_time_label) {
        char time_str[16];
        format_time(current->duration, time_str);
        lv_label_set_text(total_time_label, time_str);
    }
}

static void format_time(uint32_t seconds, char *buffer)
{
    uint32_t minutes = seconds / 60;
    seconds = seconds % 60;
    snprintf(buffer, 16, "%d:%02d", minutes, seconds);
}

// 事件回调函数
static void play_btn_event_cb(lv_event_t *e)
{
    if (g_music_player.state == PLAYER_STATE_PLAYING) {
        music_player_pause();
    } else if (g_music_player.state == PLAYER_STATE_PAUSED) {
        music_player_resume();
    } else {
        // 播放当前选中的歌曲
        if (g_music_player.playlist && g_music_player.playlist_count > 0) {
            music_player_play(g_music_player.current_index);
        }
    }
}

static void prev_btn_event_cb(lv_event_t *e)
{
    music_player_previous();
}

static void next_btn_event_cb(lv_event_t *e)
{
    music_player_next();
}

static void playlist_item_event_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    music_player_play(index);
}
