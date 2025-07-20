/*
 * 音乐播放测试版本 - test_music_main.c
 * 
 * 这是一个简化的测试版本，专门用于测试音乐播放功能
 * 开机后直接初始化SD卡并播放 /sdcard/music/a.mp3 音乐文件
 * 不包含UI界面，专注于音频播放测试
 */

#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"

// 包含必要的模块
#include "sdcard.h"
#include "music_player.h"

static const char *TAG = "music_test";

/**
 * @brief 初始化基本的GPIO设置
 */
static esp_err_t init_basic_gpio(void)
{
    ESP_LOGI(TAG, "Initializing basic GPIO for music test");
    
    // Set GPIO11 high - 屏幕电源使能（即使不用屏幕也需要保持系统稳定）
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << 11,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(11, 1);
    
    ESP_LOGI(TAG, "Basic GPIO initialized");
    return ESP_OK;
}

/**
 * @brief 测试直接播放指定MP3文件
 */
static void test_direct_mp3_playback(void)
{
    ESP_LOGI(TAG, "🎵 Starting direct MP3 playback test...");
    
    // 检查SD卡是否可访问
    DIR *root_dir = opendir("/sdcard");
    if (!root_dir) {
        ESP_LOGE(TAG, "❌ Cannot access SD card root directory");
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
                    // 检查是否是音乐目录
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
        ESP_LOGE(TAG, "❌ Music directory not found. Please create /sdcard/music/ directory");
        ESP_LOGE(TAG, "❌ And place a.mp3 file in it");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Found music directory: %s", music_dir_name);
    
    // 构建测试文件路径
    char test_file[512];
    snprintf(test_file, sizeof(test_file), "/sdcard/%s/a.mp3", music_dir_name);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(test_file, &st) != 0) {
        ESP_LOGE(TAG, "❌ Test file not found: %s", test_file);
        ESP_LOGE(TAG, "❌ Please place a.mp3 file in /sdcard/music/ directory");
        return;
    }
    
    ESP_LOGI(TAG, "✅ Test file found: %s (size: %ld bytes)", test_file, st.st_size);
    
    // 使用音乐播放器的直接测试函数
    ESP_LOGI(TAG, "🎵 Starting music playback test...");
    music_player_test_play_direct();
    
    ESP_LOGI(TAG, "🎉 Music playback test initiated!");
    ESP_LOGI(TAG, "🎵 If you can hear music, the test is successful!");
}

/**
 * @brief 主函数 - 音乐播放测试版本
 */
void app_main(void)
{
    ESP_LOGI(TAG, "🎵 === ESP32 Watch Music Player Test ===");
    ESP_LOGI(TAG, "🎵 Testing direct MP3 playback without UI");
    
    // 1. 初始化基本GPIO
    ESP_LOGI(TAG, "🔧 Step 1: Initializing basic GPIO...");
    esp_err_t gpio_ret = init_basic_gpio();
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize GPIO: %s", esp_err_to_name(gpio_ret));
        return;
    }
    ESP_LOGI(TAG, "✅ Basic GPIO initialized");
    
    // 2. 初始化SD卡
    ESP_LOGI(TAG, "🔧 Step 2: Initializing SD card...");
    esp_err_t sd_ret = sdcard_init();
    if (sd_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize SD card: %s", esp_err_to_name(sd_ret));
        ESP_LOGE(TAG, "❌ Please check:");
        ESP_LOGE(TAG, "   - SD card is properly inserted");
        ESP_LOGE(TAG, "   - SD card is formatted (FAT32 recommended)");
        ESP_LOGE(TAG, "   - Hardware connections are correct");
        return;
    }
    ESP_LOGI(TAG, "✅ SD card initialized successfully");
    
    // 显示SD卡信息
    sd_card_info_t sd_info;
    if (sdcard_get_info(&sd_info) == ESP_OK) {
        char total_size[32], used_size[32];
        sdcard_format_size(sd_info.total_bytes, total_size, sizeof(total_size));
        sdcard_format_size(sd_info.used_bytes, used_size, sizeof(used_size));
        
        ESP_LOGI(TAG, "📱 SD Card Info:");
        ESP_LOGI(TAG, "   Name: %s", sd_info.card_name);
        ESP_LOGI(TAG, "   Total: %s", total_size);
        ESP_LOGI(TAG, "   Used: %s", used_size);
    }
    
    // 等待SD卡稳定
    ESP_LOGI(TAG, "⏳ Waiting for SD card to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 初始化音乐播放器
    ESP_LOGI(TAG, "🔧 Step 3: Initializing music player...");
    esp_err_t music_ret = music_player_init();
    if (music_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize music player: %s", esp_err_to_name(music_ret));
        ESP_LOGE(TAG, "❌ Audio hardware may not be properly configured");
        return;
    }
    ESP_LOGI(TAG, "✅ Music player initialized successfully");
    
    // 等待音频系统稳定
    ESP_LOGI(TAG, "⏳ Waiting for audio system to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 4. 开始播放测试
    ESP_LOGI(TAG, "🔧 Step 4: Starting playback test...");
    test_direct_mp3_playback();
    
    ESP_LOGI(TAG, "🎉 === Music Player Test Setup Completed ===");
    ESP_LOGI(TAG, "🎵 System Status:");
    ESP_LOGI(TAG, "   - SD Card: ✅ Initialized");
    ESP_LOGI(TAG, "   - Audio System: ✅ Initialized");
    ESP_LOGI(TAG, "   - Music Player: ✅ Ready");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎧 If you can hear audio output, the test is successful!");
    ESP_LOGI(TAG, "🔊 Expected behavior:");
    ESP_LOGI(TAG, "   - Should play /sdcard/music/a.mp3");
    ESP_LOGI(TAG, "   - Audio should be clear without distortion");
    ESP_LOGI(TAG, "   - Playback should continue automatically");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📝 Troubleshooting tips:");
    ESP_LOGI(TAG, "   - Ensure a.mp3 exists in /sdcard/music/");
    ESP_LOGI(TAG, "   - Check audio connections (speakers/headphones)");
    ESP_LOGI(TAG, "   - Verify MP3 file is not corrupted");
    ESP_LOGI(TAG, "   - Check volume levels");
    
    // 5. 主循环 - 保持程序运行并监控状态
    int loop_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒输出一次状态
        loop_count++;
        
        ESP_LOGI(TAG, "🎵 [%d] Music test running... (%.1f minutes)", 
                 loop_count, (loop_count * 5.0) / 60.0);
        
        // 每分钟检查一次播放器状态
        if (loop_count % 12 == 0) { // 12 * 5秒 = 1分钟
            player_state_t state = music_player_get_state();
            const char* current_title = music_player_get_current_title();
            float volume = music_player_get_volume();
            
            ESP_LOGI(TAG, "🎵 Player Status:");
            ESP_LOGI(TAG, "   State: %s", 
                     (state == PLAYER_STATE_PLAYING) ? "Playing" :
                     (state == PLAYER_STATE_PAUSED) ? "Paused" : "Stopped");
            ESP_LOGI(TAG, "   Current: %s", current_title);
            ESP_LOGI(TAG, "   Volume: %.0f%%", volume * 100);
        }
        
        // 每10分钟提醒一次
        if (loop_count % 120 == 0) { // 120 * 5秒 = 10分钟
            ESP_LOGI(TAG, "🎵 Music test has been running for %.1f minutes", 
                     (loop_count * 5.0) / 60.0);
            ESP_LOGI(TAG, "🎵 Press RESET button to restart the test");
        }
    }
}
