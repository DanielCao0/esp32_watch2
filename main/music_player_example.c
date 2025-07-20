/*
 * Music Player Task Example
 * 
 * 这个文件展示了如何在main.c中使用任务化的音乐播放器
 */

#include "music_player.h"
#include "esp_log.h"

static const char *TAG = "MUSIC_EXAMPLE";

void music_player_example_usage(void)
{
    ESP_LOGI(TAG, "=== Music Player Task Example ===");
    
    // 1. 初始化音乐播放器（这会创建音频系统和任务）
    esp_err_t ret = music_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize music player");
        return;
    }
    ESP_LOGI(TAG, "✅ Music player initialized");
    
    // 2. 创建UI界面
    lv_obj_t *music_screen = music_player_create();
    if (music_screen) {
        ESP_LOGI(TAG, "✅ Music player UI created");
    }
    
    // 3. 扫描文件（异步执行）
    ret = music_player_scan_files();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ File scan request sent to task");
    }
    
    // 4. 等待一段时间让扫描完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 5. 播放第一首歌（如果有的话）
    if (music_player_get_state() == PLAYER_STATE_STOPPED) {
        ret = music_player_play(0);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ Play request sent to task");
        }
    }
    
    // 6. 演示其他控制功能
    vTaskDelay(pdMS_TO_TICKS(5000)); // 播放5秒
    
    // 暂停
    music_player_pause();
    ESP_LOGI(TAG, "✅ Pause request sent to task");
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // 暂停2秒
    
    // 恢复
    music_player_resume();
    ESP_LOGI(TAG, "✅ Resume request sent to task");
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 播放3秒
    
    // 下一首
    music_player_next();
    ESP_LOGI(TAG, "✅ Next request sent to task");
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 播放3秒
    
    // 调整音量
    music_player_set_volume(0.5f); // 50% 音量
    ESP_LOGI(TAG, "✅ Volume change request sent to task");
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // 播放3秒
    
    // 停止播放
    music_player_stop();
    ESP_LOGI(TAG, "✅ Stop request sent to task");
    
    ESP_LOGI(TAG, "=== Music Player Task Example Completed ===");
}

void music_player_task_management_example(void)
{
    ESP_LOGI(TAG, "=== Task Management Example ===");
    
    // 暂停音乐播放器任务
    music_player_task_suspend();
    ESP_LOGI(TAG, "🔒 Music player task suspended");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 恢复音乐播放器任务
    music_player_task_resume();
    ESP_LOGI(TAG, "🔓 Music player task resumed");
    
    // 最终清理
    music_player_deinit();
    ESP_LOGI(TAG, "🧹 Music player deinitialized");
    
    ESP_LOGI(TAG, "=== Task Management Example Completed ===");
}

/*
 * 在你的 main.c 中的使用方法：
 * 
 * void app_main(void)
 * {
 *     // ... 其他初始化代码 ...
 *     
 *     // 初始化音乐播放器
 *     music_player_init();
 *     
 *     // 创建UI
 *     lv_obj_t *music_screen = music_player_create();
 *     
 *     // 切换到音乐界面
 *     lv_scr_load(music_screen);
 *     
 *     // ... 主循环 ...
 *     
 *     // 程序结束时清理
 *     music_player_deinit();
 * }
 */
