#include "music_player.h"
#include "sdcard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MUSIC_PLAYER_TEST";

void music_player_test_task(void *arg)
{
    ESP_LOGI(TAG, "Starting music player test...");
    
    // 等待SD卡初始化
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 初始化音乐播放器
    esp_err_t ret = music_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize music player: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // 扫描MP3文件
    ret = music_player_scan_files();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan music files: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // 创建音乐播放器UI
    lv_obj_t *music_screen = music_player_create();
    if (music_screen) {
        ESP_LOGI(TAG, "Music player UI created successfully");
    }
    
    ESP_LOGI(TAG, "Music player test completed");
    
    // 可选：测试播放功能
    /*
    if (music_player_get_state() == PLAYER_STATE_STOPPED) {
        ESP_LOGI(TAG, "Testing playback of first song...");
        music_player_play(0);
        
        // 播放10秒后暂停
        vTaskDelay(pdMS_TO_TICKS(10000));
        music_player_pause();
        ESP_LOGI(TAG, "Playback paused");
        
        // 等待2秒后恢复
        vTaskDelay(pdMS_TO_TICKS(2000));
        music_player_resume();
        ESP_LOGI(TAG, "Playback resumed");
        
        // 播放5秒后停止
        vTaskDelay(pdMS_TO_TICKS(5000));
        music_player_stop();
        ESP_LOGI(TAG, "Playback stopped");
    }
    */
    
    vTaskDelete(NULL);
}

esp_err_t music_player_test_start(void)
{
    BaseType_t ret = xTaskCreate(
        music_player_test_task,
        "music_test",
        4096,
        NULL,
        5,
        NULL
    );
    
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
