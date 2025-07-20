#include "music_player.h"
#include "sdcard.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "audio_player.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "MUSIC_PLAYER";

// 音频硬件配置
#define CONFIG_BSP_I2S_NUM 1

/* Audio */
#define BSP_I2S_SCLK          (GPIO_NUM_6)
#define BSP_I2S_MCLK          -1
#define BSP_I2S_LCLK          (GPIO_NUM_7)
#define BSP_I2S_DOUT          GPIO_NUM_5
#define BSP_I2S_DSIN          -1 // From ADC ES7210
#define BSP_MAX98357_ENABLE   (GPIO_NUM_4)  // MAX98357 Enable Pin

/**
 * @brief ESP-BOX I2S pinout
 */
#define BSP_I2S_GPIO_CFG       \
    {                          \
        .mclk = BSP_I2S_MCLK,  \
        .bclk = BSP_I2S_SCLK,  \
        .ws = BSP_I2S_LCLK,    \
        .dout = BSP_I2S_DOUT,  \
        .din = BSP_I2S_DSIN,   \
        .invert_flags = {      \
            .mclk_inv = false, \
            .bclk_inv = false, \
            .ws_inv = false,   \
        },                     \
    }

/**
 * @brief Mono Duplex I2S configuration structure
 */
#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG,                                                                 \
    }

// 全局播放器实例
static music_player_t g_music_player = {0};
static lv_obj_t *music_screen = NULL;

// 任务相关变量
#define MUSIC_PLAYER_TASK_STACK_SIZE    (8192)
#define MUSIC_PLAYER_TASK_PRIORITY      (5)
#define MUSIC_PLAYER_QUEUE_SIZE         (10)

// 任务消息类型
typedef enum {
    MUSIC_TASK_MSG_PLAY = 0,
    MUSIC_TASK_MSG_PAUSE,
    MUSIC_TASK_MSG_RESUME,
    MUSIC_TASK_MSG_STOP,
    MUSIC_TASK_MSG_NEXT,
    MUSIC_TASK_MSG_PREVIOUS,
    MUSIC_TASK_MSG_SET_VOLUME,
    MUSIC_TASK_MSG_SCAN_FILES,
    MUSIC_TASK_MSG_EXIT
} music_task_msg_type_t;

// 任务消息结构
typedef struct {
    music_task_msg_type_t type;
    union {
        int index;      // 用于播放指定索引
        float volume;   // 用于设置音量
    } data;
} music_task_msg_t;

static TaskHandle_t music_player_task_handle = NULL;
static QueueHandle_t music_task_queue = NULL;
static SemaphoreHandle_t music_task_mutex = NULL;

// 音频播放器相关变量
static i2s_chan_handle_t i2s_tx_chan;
static i2s_chan_handle_t i2s_rx_chan;
static float volume_gain = 1.0f; // 音量增益系数 (1.0 = 100%, 0.5 = 50%, 2.0 = 200%)
static bool audio_initialized = false;
static bool playback_completed = false;
static FILE *current_music_file = NULL;

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
static esp_err_t scan_directory_recursive(const char *dir_path, int *mp3_count, music_file_t **playlist, int *current_index, int max_files);

// 音频相关函数
static esp_err_t bsp_i2s_write(void * audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);
static esp_err_t bsp_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);
static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting);
static esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config, i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel);
static void audio_player_callback(audio_player_cb_ctx_t *ctx);
static esp_err_t audio_system_init(void);
static esp_err_t audio_system_deinit(void);
static esp_err_t check_sdcard_health(void);

// 任务相关函数
static void music_player_task(void *pvParameters);
static esp_err_t music_task_send_message(music_task_msg_type_t type, int data_int, float data_float);
static esp_err_t music_player_play_internal(int index);
static esp_err_t music_player_pause_internal(void);
static esp_err_t music_player_resume_internal(void);
static esp_err_t music_player_stop_internal(void);
static esp_err_t music_player_next_internal(void);
static esp_err_t music_player_previous_internal(void);
static void music_player_set_volume_internal(float volume);
static esp_err_t music_player_scan_files_internal(void);
static esp_err_t audio_system_deinit(void);
static esp_err_t check_sdcard_health(void);

esp_err_t music_player_scan_files(void)
{
    // 通过任务执行文件扫描操作
    return music_task_send_message(MUSIC_TASK_MSG_SCAN_FILES, 0, 0.0f);
}

static esp_err_t music_player_scan_files_internal(void)
{
    ESP_LOGI(TAG, "Scanning SD card recursively for MP3 files...");
    
    // 检查SD卡是否正常挂载
    struct stat sdcard_stat;
    if (stat("/sdcard", &sdcard_stat) != 0) {
        ESP_LOGE(TAG, "SD card not accessible");
        return ESP_FAIL;
    }
    
    // 释放之前的播放列表
    if (g_music_player.playlist) {
        heap_caps_free(g_music_player.playlist);
        g_music_player.playlist = NULL;
        g_music_player.playlist_count = 0;
    }
    
    // 第一次遍历：计算MP3文件数量
    int mp3_count = 0;
    int temp_index = 0;
    const int max_files = 1000; // 设置最大文件数限制
    
    esp_err_t ret = scan_directory_recursive("/sdcard", &mp3_count, NULL, &temp_index, max_files);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan SD card directory");
        return ret;
    }
    
    if (mp3_count == 0) {
        ESP_LOGW(TAG, "No MP3 files found in SD card");
        return ESP_OK;
    }
    
    // 限制实际分配的内存数量
    int actual_count = (mp3_count > max_files) ? max_files : mp3_count;
    
    // 使用PSRAM分配播放列表内存
    g_music_player.playlist = heap_caps_malloc(actual_count * sizeof(music_file_t), 
                                               MALLOC_CAP_SPIRAM);
    if (!g_music_player.playlist) {
        ESP_LOGE(TAG, "Failed to allocate playlist memory for %d files", actual_count);
        return ESP_ERR_NO_MEM;
    }
    
    // 第二次遍历：填充播放列表
    int current_index = 0;
    ret = scan_directory_recursive("/sdcard", &mp3_count, &g_music_player.playlist, &current_index, actual_count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Some directories failed to scan, but continuing...");
    }
    
    g_music_player.playlist_count = current_index;
    g_music_player.current_index = 0;
    g_music_player.state = PLAYER_STATE_STOPPED;
    
    ESP_LOGI(TAG, "Found %d MP3 files (total scanned: %d)", g_music_player.playlist_count, mp3_count);
    
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
    
    // 通过任务执行播放操作
    return music_task_send_message(MUSIC_TASK_MSG_PLAY, index, 0.0f);
}

static esp_err_t music_player_play_internal(int index)
{
    if (!g_music_player.playlist || index >= g_music_player.playlist_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting playback for index %d", index);
    
    // 检查SD卡是否正常挂载
    struct stat sdcard_stat;
    if (stat("/sdcard", &sdcard_stat) != 0) {
        ESP_LOGE(TAG, "SD card not accessible: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    // 给SD卡一些时间稳定，特别是在连续操作之后
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 执行SD卡健康检查（非阻塞）
    esp_err_t health_check = check_sdcard_health();
    if (health_check != ESP_OK) {
        ESP_LOGW(TAG, "SD card health check failed, but continuing with playback attempt");
        ESP_LOGW(TAG, "The SD card may be read-only or have limited write access");
    }
    
    // 初始化音频系统（如果还没有初始化）
    esp_err_t ret = audio_system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio system");
        return ret;
    }
    
    // 停止当前播放
    if (g_music_player.state == PLAYER_STATE_PLAYING) {
        music_player_stop();
        vTaskDelay(pdMS_TO_TICKS(100)); // 短暂等待停止完成
    }
    
    // 关闭之前的文件
    if (current_music_file) {
        fclose(current_music_file);
        current_music_file = NULL;
    }
    
    g_music_player.current_index = index;
    g_music_player.current_position = 0;
    playback_completed = false;
    
    music_file_t *current = &g_music_player.playlist[index];
    ESP_LOGI(TAG, "Playing: %s", current->title);
    ESP_LOGI(TAG, "Full path: %s", current->filepath);
    ESP_LOGI(TAG, "File size: %zu bytes", current->file_size);
    
    // 检查文件是否存在，增加重试机制
    struct stat file_stat;
    int stat_retry = 3; // 重试3次
    esp_err_t stat_result = ESP_FAIL;
    
    while (stat_retry > 0) {
        if (stat(current->filepath, &file_stat) == 0) {
            stat_result = ESP_OK;
            break;
        }
        
        ESP_LOGW(TAG, "Failed to stat file (retry %d/3): %s, error: %s", 
                 4 - stat_retry, current->filepath, strerror(errno));
        
        stat_retry--;
        if (stat_retry > 0) {
            // 短暂延时后重试
            vTaskDelay(pdMS_TO_TICKS(100));
            
            // 检查SD卡是否还可访问
            struct stat sdcard_check;
            if (stat("/sdcard", &sdcard_check) != 0) {
                ESP_LOGE(TAG, "SD card became inaccessible during stat retry");
                return ESP_FAIL;
            }
        }
    }
    
    if (stat_result != ESP_OK) {
        ESP_LOGE(TAG, "File does not exist or cannot be accessed after retries: %s", current->filepath);
        ESP_LOGE(TAG, "This may indicate SD card hardware issues or file was removed");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "File exists, size: %lld bytes", file_stat.st_size);
    
    // 验证文件大小是否匹配
    if (file_stat.st_size != current->file_size) {
        ESP_LOGW(TAG, "File size mismatch: expected %zu, actual %lld", 
                 current->file_size, file_stat.st_size);
        current->file_size = file_stat.st_size; // 更新文件大小
    }
    
    // SD卡健康检查 - 尝试读取一小部分文件来验证SD卡状态
    FILE *test_file = NULL;
    int test_retry = 3;
    bool test_passed = false;
    
    while (test_retry > 0 && !test_passed) {
        test_file = fopen(current->filepath, "rb");
        if (test_file) {
            char test_buffer[512];
            size_t read_bytes = fread(test_buffer, 1, sizeof(test_buffer), test_file);
            fclose(test_file);
            
            if (read_bytes > 0) {
                ESP_LOGI(TAG, "SD card health check passed - read %zu bytes", read_bytes);
                test_passed = true;
                break;
            } else {
                ESP_LOGW(TAG, "SD card read test failed - cannot read file data (retry %d/3)", 4 - test_retry);
            }
        } else {
            ESP_LOGW(TAG, "SD card health check failed - cannot open file for test read (retry %d/3): %s", 
                     4 - test_retry, strerror(errno));
        }
        
        test_retry--;
        if (test_retry > 0) {
            vTaskDelay(pdMS_TO_TICKS(200)); // 增加延时
        }
    }
    
    if (!test_passed) {
        ESP_LOGE(TAG, "SD card health check failed after all retries");
        return ESP_FAIL;
    }
    
    // 短暂延时让SD卡稳定
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 打开MP3文件，带重试机制和更详细的错误处理
    int retry_count = 5; // 增加重试次数
    while (retry_count > 0) {
        current_music_file = fopen(current->filepath, "rb");
        if (current_music_file) {
            ESP_LOGI(TAG, "Successfully opened file: %s", current->filepath);
            
            // 验证文件可以正常读取
            char test_buffer[1024];
            size_t read_test = fread(test_buffer, 1, sizeof(test_buffer), current_music_file);
            if (read_test > 0) {
                // 重置文件指针到开始
                fseek(current_music_file, 0, SEEK_SET);
                ESP_LOGI(TAG, "File read verification passed (%zu bytes)", read_test);
                break;
            } else {
                ESP_LOGW(TAG, "File opened but cannot read data, closing and retrying...");
                fclose(current_music_file);
                current_music_file = NULL;
            }
        }
        
        ESP_LOGW(TAG, "Failed to open/read file (retry %d/5): %s, error: %s", 
                 6 - retry_count, current->filepath, strerror(errno));
        retry_count--;
        
        if (retry_count > 0) {
            // 增加延时，让SD卡有时间恢复
            vTaskDelay(pdMS_TO_TICKS(300 + (5 - retry_count) * 200)); 
            
            // 在重试前再次检查SD卡状态
            struct stat retry_stat;
            if (stat("/sdcard", &retry_stat) != 0) {
                ESP_LOGE(TAG, "SD card became inaccessible during retry, error: %s", strerror(errno));
                break;
            }
            
            // 检查目标文件是否仍然存在
            struct stat file_check;
            if (stat(current->filepath, &file_check) != 0) {
                ESP_LOGW(TAG, "Target file no longer accessible during retry: %s, error: %s", 
                         current->filepath, strerror(errno));
                // 继续重试，可能是临时的SD卡问题
            } else {
                ESP_LOGI(TAG, "Target file still exists, size: %lld bytes", file_check.st_size);
            }
        }
    }
    
    if (!current_music_file) {
        ESP_LOGE(TAG, "Failed to open file after %d retries: %s", 5, current->filepath);
        ESP_LOGE(TAG, "This may indicate SD card hardware issues or file system corruption");
        return ESP_FAIL;
    }
    
    // 开始播放
    ret = audio_player_play(current_music_file);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start playback: %s", esp_err_to_name(ret));
        fclose(current_music_file);
        current_music_file = NULL;
        return ret;
    }
    
    update_ui_info();
    return ESP_OK;
}

esp_err_t music_player_pause(void)
{
    // 通过任务执行暂停操作
    return music_task_send_message(MUSIC_TASK_MSG_PAUSE, 0, 0.0f);
}

static esp_err_t music_player_pause_internal(void)
{
    if (g_music_player.state != PLAYER_STATE_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 暂停音频播放器
    esp_err_t ret = audio_player_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to pause playback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_music_player.state = PLAYER_STATE_PAUSED;
    ESP_LOGI(TAG, "Music paused");
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        if (label) {
            lv_label_set_text(label, LV_SYMBOL_PLAY);
        }
    }
    
    return ESP_OK;
}

esp_err_t music_player_resume(void)
{
    // 通过任务执行恢复操作
    return music_task_send_message(MUSIC_TASK_MSG_RESUME, 0, 0.0f);
}

static esp_err_t music_player_resume_internal(void)
{
    if (g_music_player.state != PLAYER_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 重新开始播放当前文件（由于audio_player库的限制，暂停后需要重新开始）
    if (current_music_file && g_music_player.playlist && g_music_player.current_index < g_music_player.playlist_count) {
        // 重置文件指针到开始位置
        fseek(current_music_file, 0, SEEK_SET);
        
        esp_err_t ret = audio_player_play(current_music_file);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to resume playback: %s", esp_err_to_name(ret));
            return ret;
        }
        
        g_music_player.state = PLAYER_STATE_PLAYING;
        ESP_LOGI(TAG, "Music resumed");
        
        // 更新播放按钮图标
        if (play_btn) {
            lv_obj_t *label = lv_obj_get_child(play_btn, 0);
            if (label) {
                lv_label_set_text(label, LV_SYMBOL_PAUSE);
            }
        }
        
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_STATE;
}

esp_err_t music_player_stop(void)
{
    // 通过任务执行停止操作
    return music_task_send_message(MUSIC_TASK_MSG_STOP, 0, 0.0f);
}

static esp_err_t music_player_stop_internal(void)
{
    ESP_LOGI(TAG, "Music stopped");
    
    // 停止音频播放器
    if (audio_initialized) {
        esp_err_t ret = audio_player_stop();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to stop audio player: %s", esp_err_to_name(ret));
        }
    }
    
    // 关闭文件
    if (current_music_file) {
        fclose(current_music_file);
        current_music_file = NULL;
    }
    
    g_music_player.state = PLAYER_STATE_STOPPED;
    g_music_player.current_position = 0;
    playback_completed = false;
    
    // 更新播放按钮图标
    if (play_btn) {
        lv_obj_t *label = lv_obj_get_child(play_btn, 0);
        if (label) {
            lv_label_set_text(label, LV_SYMBOL_PLAY);
        }
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
    // 通过任务执行下一首操作
    return music_task_send_message(MUSIC_TASK_MSG_NEXT, 0, 0.0f);
}

static esp_err_t music_player_next_internal(void)
{
    if (!g_music_player.playlist || g_music_player.playlist_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int next_index = (g_music_player.current_index + 1) % g_music_player.playlist_count;
    return music_player_play_internal(next_index);
}

esp_err_t music_player_previous(void)
{
    // 通过任务执行上一首操作
    return music_task_send_message(MUSIC_TASK_MSG_PREVIOUS, 0, 0.0f);
}

static esp_err_t music_player_previous_internal(void)
{
    if (!g_music_player.playlist || g_music_player.playlist_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int prev_index = g_music_player.current_index - 1;
    if (prev_index < 0) {
        prev_index = g_music_player.playlist_count - 1;
    }
    
    return music_player_play_internal(prev_index);
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

// 音频相关函数实现
static esp_err_t bsp_i2s_write(void * audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    // 调整音频数据幅度
    int16_t *samples = (int16_t *)audio_buffer;
    size_t sample_count = len / sizeof(int16_t);
    
    for (size_t i = 0; i < sample_count; i++) {
        int32_t amplified = (int32_t)(samples[i] * volume_gain);
        // 防止溢出
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        samples[i] = (int16_t)amplified;
    }
    
    esp_err_t ret = i2s_channel_write(i2s_tx_chan, (char *)audio_buffer, len, bytes_written, timeout_ms);
    return ret;
}

static esp_err_t bsp_i2s_reconfig_clk(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bits_cfg, (i2s_slot_mode_t)ch),
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };

    ret |= i2s_channel_disable(i2s_tx_chan);
    ret |= i2s_channel_reconfig_std_clock(i2s_tx_chan, &std_cfg.clk_cfg);
    ret |= i2s_channel_reconfig_std_slot(i2s_tx_chan, &std_cfg.slot_cfg);
    ret |= i2s_channel_enable(i2s_tx_chan);
    return ret;
}

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting) 
{
    ESP_LOGI(TAG, "mute setting %d", setting);
    return ESP_OK;
}

static esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config, i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel)
{
    /* Setup I2S peripheral */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_channel, rx_channel));

    /* Setup I2S channels */
    const i2s_std_config_t std_cfg_default = BSP_I2S_DUPLEX_MONO_CFG(44100);  // 使用44.1kHz提高音质
    const i2s_std_config_t *p_i2s_cfg = &std_cfg_default;
    if (i2s_config != NULL) {
        p_i2s_cfg = i2s_config;
    }

    if (tx_channel != NULL) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_channel, p_i2s_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(*tx_channel));
    }
    if (rx_channel != NULL) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(*rx_channel, p_i2s_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(*rx_channel));
    }

    /* Setup MAX98357 enable pin */
    const gpio_config_t max98357_io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(BSP_MAX98357_ENABLE),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&max98357_io_conf));
    ESP_ERROR_CHECK(gpio_set_level(BSP_MAX98357_ENABLE, 1)); // Enable MAX98357

    return ESP_OK;
}

static void audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    ESP_LOGI(TAG, "Audio callback event: %d", ctx->audio_event);
    
    switch(ctx->audio_event) {
        case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
            ESP_LOGI(TAG, "Audio playback started");
            g_music_player.state = PLAYER_STATE_PLAYING;
            
            // 更新播放按钮图标
            if (play_btn) {
                lv_obj_t *label = lv_obj_get_child(play_btn, 0);
                if (label) {
                    lv_label_set_text(label, LV_SYMBOL_PAUSE);
                }
            }
            break;
            
        case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
            ESP_LOGI(TAG, "Audio playback completed");
            playback_completed = true;
            g_music_player.state = PLAYER_STATE_STOPPED;
            
            // 更新播放按钮图标
            if (play_btn) {
                lv_obj_t *label = lv_obj_get_child(play_btn, 0);
                if (label) {
                    lv_label_set_text(label, LV_SYMBOL_PLAY);
                }
            }
            
            // 重置进度条
            if (progress_bar) {
                lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
            }
            
            if (current_time_label) {
                lv_label_set_text(current_time_label, "0:00");
            }
            
            // 关闭文件
            if (current_music_file) {
                fclose(current_music_file);
                current_music_file = NULL;
            }
            
            // 自动播放下一首（如果需要）
            if (g_music_player.playlist && g_music_player.playlist_count > 0) {
                vTaskDelay(pdMS_TO_TICKS(500)); // 短暂延时
                music_player_next();
            }
            break;
            
        case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
            ESP_LOGI(TAG, "Audio player shutdown");
            g_music_player.state = PLAYER_STATE_STOPPED;
            break;
            
        default:
            ESP_LOGI(TAG, "Unknown audio event: %d", ctx->audio_event);
            break;
    }
}

static esp_err_t audio_system_init(void)
{
    if (audio_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing audio system...");
    
    /* Configure I2S peripheral */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = BSP_I2S_GPIO_CFG,
    };
    
    esp_err_t ret = bsp_audio_init(&std_cfg, &i2s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize audio player */
    audio_player_config_t config = { 
        .mute_fn = audio_mute_function,
        .write_fn = bsp_i2s_write,
        .clk_set_fn = bsp_i2s_reconfig_clk,
        .priority = 10,
        .coreID = 0 
    };
    
    ret = audio_player_new(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create audio player: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Register audio callback */
    ret = audio_player_callback_register(audio_player_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register audio callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    audio_initialized = true;
    ESP_LOGI(TAG, "Audio system initialized successfully");
    return ESP_OK;
}

static esp_err_t audio_system_deinit(void)
{
    if (!audio_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing audio system...");
    
    // 停止当前播放
    music_player_stop();
    
    // 删除音频播放器
    esp_err_t ret = audio_player_delete();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete audio player: %s", esp_err_to_name(ret));
    }
    
    // 禁用和删除I2S通道
    if (i2s_tx_chan) {
        i2s_channel_disable(i2s_tx_chan);
        i2s_del_channel(i2s_tx_chan);
        i2s_tx_chan = NULL;
    }
    
    if (i2s_rx_chan) {
        i2s_channel_disable(i2s_rx_chan);
        i2s_del_channel(i2s_rx_chan);
        i2s_rx_chan = NULL;
    }
    
    audio_initialized = false;
    ESP_LOGI(TAG, "Audio system deinitialized");
    return ret;
}

// 新增的公共函数实现
esp_err_t music_player_init(void)
{
    esp_err_t ret;
    
    // 首先初始化音频系统
    ret = audio_system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio system");
        return ret;
    }
    
    // 然后创建音乐播放器任务
    ret = music_player_task_create();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create music player task");
        audio_system_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "Music player initialized successfully");
    return ESP_OK;
}

esp_err_t music_player_deinit(void)
{
    esp_err_t ret;
    
    // 首先删除音乐播放器任务
    ret = music_player_task_delete();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete music player task properly");
    }
    
    // 然后关闭音频系统
    ret = audio_system_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize audio system properly");
    }
    
    ESP_LOGI(TAG, "Music player deinitialized");
    return ESP_OK;
}

void music_player_set_volume(float volume)
{
    // 通过任务执行音量设置操作
    music_task_send_message(MUSIC_TASK_MSG_SET_VOLUME, 0, volume);
}

static void music_player_set_volume_internal(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 2.0f) volume = 2.0f;
    
    volume_gain = volume;
    ESP_LOGI(TAG, "Volume set to: %.1f%%", volume * 100);
}

float music_player_get_volume(void)
{
    return volume_gain;
}

player_state_t music_player_get_state(void)
{
    return g_music_player.state;
}

int music_player_get_current_index(void)
{
    return g_music_player.current_index;
}

const char* music_player_get_current_title(void)
{
    if (!g_music_player.playlist || g_music_player.current_index >= g_music_player.playlist_count) {
        return "No Song";
    }
    
    return g_music_player.playlist[g_music_player.current_index].title;
}

// 递归扫描目录中的MP3文件
static esp_err_t scan_directory_recursive(const char *dir_path, int *mp3_count, music_file_t **playlist, int *current_index, int max_files)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open directory: %s", dir_path);
        return ESP_FAIL;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // 在第二次扫描时检查索引限制
        if (playlist && *current_index >= max_files) {
            break;
        }
        
        // 跳过当前目录和上级目录
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 构建完整路径
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat file_stat;
        if (stat(full_path, &file_stat) != 0) {
            continue;
        }
        
        if (S_ISDIR(file_stat.st_mode)) {
            // 如果是目录，递归扫描
            ESP_LOGI(TAG, "Scanning subdirectory: %s", full_path);
            scan_directory_recursive(full_path, mp3_count, playlist, current_index, max_files);
        } else if (S_ISREG(file_stat.st_mode) && is_mp3_file(entry->d_name)) {
            // 如果是MP3文件
            (*mp3_count)++;
            
            if (playlist && *playlist && *current_index < max_files) {
                music_file_t *file = &(*playlist)[*current_index];
                
                // 填充文件信息
                strncpy(file->filename, entry->d_name, sizeof(file->filename) - 1);
                file->filename[sizeof(file->filename) - 1] = '\0';
                
                strncpy(file->filepath, full_path, sizeof(file->filepath) - 1);
                file->filepath[sizeof(file->filepath) - 1] = '\0';
                
                // 提取标题（去掉.mp3扩展名）
                strncpy(file->title, entry->d_name, sizeof(file->title) - 1);
                file->title[sizeof(file->title) - 1] = '\0';
                char *dot = strrchr(file->title, '.');
                if (dot) *dot = '\0';
                
                // 默认艺术家信息
                strcpy(file->artist, "Unknown Artist");
                
                // 获取文件大小
                file->file_size = file_stat.st_size;
                
                // 估算时长（基于44.1kHz采样率，假设128kbps平均码率）
                // 计算公式：文件大小(字节) / (码率(kbps) * 1000 / 8) = 时长(秒)
                file->duration = file->file_size / (128 * 1000 / 8);
                
                ESP_LOGI(TAG, "Found MP3[%d]: %s -> %s (%zu bytes)", 
                         *current_index, entry->d_name, file->filepath, file->file_size);
                (*current_index)++;
            } else {
                ESP_LOGI(TAG, "MP3 file found but not added: %s", full_path);
            }
        }
    }
    
    closedir(dir);
    return ESP_OK;
}

// SD卡健康检查函数
static esp_err_t check_sdcard_health(void)
{
    ESP_LOGI(TAG, "Checking SD card health...");
    
    // 检查SD卡目录是否可访问
    struct stat sdcard_stat;
    if (stat("/sdcard", &sdcard_stat) != 0) {
        ESP_LOGE(TAG, "SD card mount point not accessible");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SD card mount point accessible");
    
    // 检查SD卡是否为只读
    if (access("/sdcard", W_OK) != 0) {
        ESP_LOGW(TAG, "SD card appears to be read-only or write protected");
        ESP_LOGI(TAG, "Performing read-only health check");
        
        // 只读模式下的健康检查 - 尝试读取根目录
        DIR *test_dir = opendir("/sdcard");
        if (!test_dir) {
            ESP_LOGE(TAG, "Cannot read SD card root directory");
            return ESP_FAIL;
        }
        
        struct dirent *entry;
        int file_count = 0;
        while ((entry = readdir(test_dir)) != NULL && file_count < 5) {
            file_count++;
        }
        closedir(test_dir);
        
        ESP_LOGI(TAG, "SD card read-only health check passed (found %d entries)", file_count);
        return ESP_OK;
    }
    
    // 尝试创建一个测试文件
    const char *test_file_path = "/sdcard/health_test.tmp";
    FILE *test_file = fopen(test_file_path, "w");
    if (!test_file) {
        ESP_LOGE(TAG, "Cannot create test file on SD card (errno: %d)", errno);
        ESP_LOGE(TAG, "This may indicate SD card is write-protected or filesystem is corrupted");
        
        // 尝试备用的只读健康检查方法
        ESP_LOGI(TAG, "Attempting read-only health check as fallback...");
        DIR *test_dir = opendir("/sdcard");
        if (!test_dir) {
            ESP_LOGE(TAG, "Cannot read SD card root directory in fallback test");
            return ESP_FAIL;
        }
        
        struct dirent *entry;
        int file_count = 0;
        while ((entry = readdir(test_dir)) != NULL && file_count < 5) {
            file_count++;
        }
        closedir(test_dir);
        
        if (file_count > 0) {
            ESP_LOGI(TAG, "Fallback read-only health check passed (found %d entries)", file_count);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "SD card appears to be empty or corrupted");
            return ESP_FAIL;
        }
    }
    
    ESP_LOGI(TAG, "Test file created successfully");
    
    // 写入测试数据
    const char *test_data = "SD card health test";
    size_t written = fwrite(test_data, 1, strlen(test_data), test_file);
    fflush(test_file); // 确保数据写入
    fclose(test_file);
    
    if (written != strlen(test_data)) {
        ESP_LOGE(TAG, "Failed to write test data to SD card (%zu/%zu bytes)", written, strlen(test_data));
        unlink(test_file_path); // 尝试删除测试文件
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Test data written successfully");
    
    // 读取测试文件验证
    test_file = fopen(test_file_path, "r");
    if (!test_file) {
        ESP_LOGE(TAG, "Cannot read back test file from SD card");
        unlink(test_file_path);
        return ESP_FAIL;
    }
    
    char read_buffer[64];
    size_t read_bytes = fread(read_buffer, 1, sizeof(read_buffer) - 1, test_file);
    fclose(test_file);
    read_buffer[read_bytes] = '\0';
    
    // 删除测试文件
    if (unlink(test_file_path) != 0) {
        ESP_LOGW(TAG, "Failed to delete test file: %s", test_file_path);
    }
    
    if (strcmp(read_buffer, test_data) != 0) {
        ESP_LOGE(TAG, "SD card data integrity test failed");
        ESP_LOGE(TAG, "Expected: '%s', Got: '%s'", test_data, read_buffer);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SD card read/write health check passed");
    return ESP_OK;
}

/**
 * @brief 直接测试MP3文件访问的函数
 * 用于菜单中的TEST功能，仅测试文件打开和关闭
 */
void music_player_test_play_direct(void) {
    ESP_LOGI(TAG, "🎵 Starting MP3 file access test...");
    
    // 0. 检查SD卡根目录
    DIR *root_dir = opendir("/sdcard");
    if (!root_dir) {
        ESP_LOGE(TAG, "❌ Cannot open SD card root directory: %s", strerror(errno));
        return;
    }
    
    ESP_LOGI(TAG, "📁 SD card root directory contents:");
    struct dirent *entry;
    bool music_dir_found = false;
    char music_dir_name[64] = "";
    while ((entry = readdir(root_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "/sdcard/%s", entry->d_name);
            struct stat file_stat;
            if (stat(full_path, &file_stat) == 0) {
                if (S_ISDIR(file_stat.st_mode)) {
                    ESP_LOGI(TAG, "  📁 %s/ (directory)", entry->d_name);
                    // 大小写不敏感检查音乐目录
                    if (strcasecmp(entry->d_name, "music") == 0) {
                        music_dir_found = true;
                        strncpy(music_dir_name, entry->d_name, sizeof(music_dir_name) - 1);
                    }
                } else {
                    ESP_LOGI(TAG, "  📄 %s (file, %lld bytes)", entry->d_name, file_stat.st_size);
                }
            }
        }
    }
    closedir(root_dir);
    
    if (!music_dir_found) {
        ESP_LOGE(TAG, "❌ Music directory not found in SD card root");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Found music directory: %s", music_dir_name);
    
    // 1. 构建测试文件路径
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "/sdcard/%s/a.mp3", music_dir_name);
    
    // 2. 检查文件是否存在
    struct stat st;
    if (stat(test_file, &st) != 0) {
        ESP_LOGE(TAG, "❌ Test file not found: %s", test_file);
        return;
    }
    ESP_LOGI(TAG, "✅ Test file found: %s (size: %ld bytes)", test_file, st.st_size);
    
    // 3. 测试文件打开和关闭
    ESP_LOGI(TAG, "🔧 Testing file open/close operations...");
    
    FILE *test_file_handle = fopen(test_file, "rb");
    if (!test_file_handle) {
        ESP_LOGE(TAG, "❌ Failed to open test file: %s", strerror(errno));
        return;
    }
    
    ESP_LOGI(TAG, "✅ File opened successfully");
    
    // 4. 测试读取少量数据
    char test_buffer[16];
    size_t read_bytes = fread(test_buffer, 1, sizeof(test_buffer), test_file_handle);
    if (read_bytes > 0) {
        ESP_LOGI(TAG, "✅ File read test successful (%zu bytes)", read_bytes);
    } else {
        ESP_LOGE(TAG, "❌ File read test failed: %s", strerror(errno));
    }
    
    // 5. 关闭文件
    fclose(test_file_handle);
    ESP_LOGI(TAG, "✅ File closed successfully");
    
    // 6. 测试音频播放功能
    ESP_LOGI(TAG, "� Testing audio playback...");
    
    //初始化音频系统（如果还没有初始化）
    if (!audio_initialized) {
        ESP_LOGI(TAG, "🔧 Initializing audio system for test...");
        esp_err_t ret = audio_system_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ Failed to initialize audio system: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "✅ Audio system initialized");
    }
    
    //停止当前播放（如果有）
    if (g_music_player.state == PLAYER_STATE_PLAYING) {
        ESP_LOGI(TAG, "⏹️ Stopping current playback...");
        music_player_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 关闭之前的文件
    if (current_music_file) {
        fclose(current_music_file);
        current_music_file = NULL;
    }
    
    // 重新打开文件用于播放
    current_music_file = fopen(test_file, "rb");
    if (!current_music_file) {
        ESP_LOGE(TAG, "❌ Failed to reopen test file for playback: %s", strerror(errno));
        return;
    }
    ESP_LOGI(TAG, "✅ Test file reopened for playback");
    
    // 开始播放测试
    esp_err_t play_ret = audio_player_play(current_music_file);
    if (play_ret == ESP_OK) {
        ESP_LOGI(TAG, "🎉 Test playback started successfully!");
        
        // 更新播放器状态
        g_music_player.state = PLAYER_STATE_PLAYING;
        
        // 显示播放信息
        ESP_LOGI(TAG, "📱 Playing: %s", strrchr(test_file, '/') ? strrchr(test_file, '/') + 1 : test_file);
        ESP_LOGI(TAG, "📂 From: %s", test_file);
        ESP_LOGI(TAG, "💾 File size: %ld bytes", st.st_size);
        
        // 估算播放时长（假设MP3比特率约128kbps）
        uint32_t estimated_duration = (st.st_size * 8) / (128 * 1000);
        ESP_LOGI(TAG, "⏱️ Estimated duration: %d seconds", estimated_duration);
        
        ESP_LOGI(TAG, "🎵 Audio playback test will continue in background...");
        
    } else {
        ESP_LOGE(TAG, "❌ Failed to start test playback: %s", esp_err_to_name(play_ret));
        
        // 关闭文件
        if (current_music_file) {
            fclose(current_music_file);
            current_music_file = NULL;
        }
    }
    
    ESP_LOGI(TAG, "🎉 MP3 file access and playback test completed!");
}

// ===== 任务管理函数实现 =====

/**
 * @brief 音乐播放器任务主函数
 */
static void music_player_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Music player task started");
    
    music_task_msg_t msg;
    
    while (1) {
        // 等待消息
        if (xQueueReceive(music_task_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // 获取互斥锁
            if (xSemaphoreTake(music_task_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                
                switch (msg.type) {
                    case MUSIC_TASK_MSG_PLAY:
                        ESP_LOGI(TAG, "Task: Processing PLAY command for index %d", msg.data.index);
                        music_player_play_internal(msg.data.index);
                        break;
                        
                    case MUSIC_TASK_MSG_PAUSE:
                        ESP_LOGI(TAG, "Task: Processing PAUSE command");
                        music_player_pause_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_RESUME:
                        ESP_LOGI(TAG, "Task: Processing RESUME command");
                        music_player_resume_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_STOP:
                        ESP_LOGI(TAG, "Task: Processing STOP command");
                        music_player_stop_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_NEXT:
                        ESP_LOGI(TAG, "Task: Processing NEXT command");
                        music_player_next_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_PREVIOUS:
                        ESP_LOGI(TAG, "Task: Processing PREVIOUS command");
                        music_player_previous_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_SET_VOLUME:
                        ESP_LOGI(TAG, "Task: Processing SET_VOLUME command (%.1f)", msg.data.volume);
                        music_player_set_volume_internal(msg.data.volume);
                        break;
                        
                    case MUSIC_TASK_MSG_SCAN_FILES:
                        ESP_LOGI(TAG, "Task: Processing SCAN_FILES command");
                        music_player_scan_files_internal();
                        break;
                        
                    case MUSIC_TASK_MSG_EXIT:
                        ESP_LOGI(TAG, "Task: Processing EXIT command");
                        xSemaphoreGive(music_task_mutex);
                        goto task_exit;
                        
                    default:
                        ESP_LOGW(TAG, "Task: Unknown message type: %d", msg.type);
                        break;
                }
                
                // 释放互斥锁
                xSemaphoreGive(music_task_mutex);
            } else {
                ESP_LOGW(TAG, "Task: Failed to acquire mutex for message type %d", msg.type);
            }
        }
    }
    
task_exit:
    ESP_LOGI(TAG, "Music player task exiting");
    vTaskDelete(NULL);
}

/**
 * @brief 发送消息到音乐播放器任务
 */
static esp_err_t music_task_send_message(music_task_msg_type_t type, int data_int, float data_float)
{
    if (!music_task_queue) {
        ESP_LOGE(TAG, "Music task queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    music_task_msg_t msg = {
        .type = type,
    };
    
    // 根据消息类型设置数据
    switch (type) {
        case MUSIC_TASK_MSG_PLAY:
            msg.data.index = data_int;
            break;
        case MUSIC_TASK_MSG_SET_VOLUME:
            msg.data.volume = data_float;
            break;
        default:
            // 其他消息类型不需要额外数据
            break;
    }
    
    if (xQueueSend(music_task_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send message to music task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 创建音乐播放器任务
 */
esp_err_t music_player_task_create(void)
{
    if (music_player_task_handle != NULL) {
        ESP_LOGW(TAG, "Music player task already exists");
        return ESP_OK;
    }
    
    // 创建消息队列
    music_task_queue = xQueueCreate(MUSIC_PLAYER_QUEUE_SIZE, sizeof(music_task_msg_t));
    if (music_task_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create music task queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建互斥锁
    music_task_mutex = xSemaphoreCreateMutex();
    if (music_task_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create music task mutex");
        vQueueDelete(music_task_queue);
        music_task_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // 创建任务
    BaseType_t result = xTaskCreate(
        music_player_task,
        "music_player",
        MUSIC_PLAYER_TASK_STACK_SIZE,
        NULL,
        MUSIC_PLAYER_TASK_PRIORITY,
        &music_player_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create music player task");
        vSemaphoreDelete(music_task_mutex);
        vQueueDelete(music_task_queue);
        music_task_mutex = NULL;
        music_task_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Music player task created successfully");
    return ESP_OK;
}

/**
 * @brief 删除音乐播放器任务
 */
esp_err_t music_player_task_delete(void)
{
    if (music_player_task_handle == NULL) {
        ESP_LOGW(TAG, "Music player task not running");
        return ESP_OK;
    }
    
    // 发送退出消息
    esp_err_t ret = music_task_send_message(MUSIC_TASK_MSG_EXIT, 0, 0.0f);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send exit message, force deleting task");
        vTaskDelete(music_player_task_handle);
    } else {
        // 等待任务自然退出
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 清理资源
    if (music_task_mutex) {
        vSemaphoreDelete(music_task_mutex);
        music_task_mutex = NULL;
    }
    
    if (music_task_queue) {
        vQueueDelete(music_task_queue);
        music_task_queue = NULL;
    }
    
    music_player_task_handle = NULL;
    
    ESP_LOGI(TAG, "Music player task deleted");
    return ESP_OK;
}

/**
 * @brief 暂停音乐播放器任务
 */
void music_player_task_suspend(void)
{
    if (music_player_task_handle) {
        vTaskSuspend(music_player_task_handle);
        ESP_LOGI(TAG, "Music player task suspended");
    }
}

/**
 * @brief 恢复音乐播放器任务
 */
void music_player_task_resume(void)
{
    if (music_player_task_handle) {
        vTaskResume(music_player_task_handle);
        ESP_LOGI(TAG, "Music player task resumed");
    }
}
