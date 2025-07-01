/**
 * @file wifi_stability_test.c
 * @brief WiFi连接稳定性测试工具
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_connect.h"

static const char *TAG = "WIFI_TEST";

// WiFi连接稳定性测试
void wifi_stability_test(void)
{
    int test_round = 1;
    int success_count = 0;
    int total_attempts = 5;  // 测试5次
    
    ESP_LOGI(TAG, "=== WiFi Stability Test Started ===");
    ESP_LOGI(TAG, "Will test %d connection attempts", total_attempts);
    
    for (int i = 0; i < total_attempts; i++) {
        ESP_LOGI(TAG, "--- Test Round %d/%d ---", i+1, total_attempts);
        
        // 完全重置WiFi状态
        wifi_complete_reset();
        vTaskDelay(pdMS_TO_TICKS(3000));  // 等待3秒
        
        // 尝试连接
        wifi_reconnect();
        
        // 等待连接结果（最多30秒）
        int wait_count = 0;
        while (wait_count < 30 && !wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            wait_count++;
            
            if (wait_count % 5 == 0) {
                ESP_LOGI(TAG, "Waiting for connection... %d/30 seconds", wait_count);
            }
        }
        
        if (wifi_is_connected()) {
            success_count++;
            ESP_LOGI(TAG, "✅ Round %d: SUCCESS (connected to: %s)", 
                     i+1, wifi_get_current_ssid());
        } else {
            ESP_LOGE(TAG, "❌ Round %d: FAILED (timeout after 30s)", i+1);
        }
        
        // 断开连接准备下次测试
        if (wifi_is_connected()) {
            ESP_LOGI(TAG, "Disconnecting for next test...");
            // 这里可以添加断开逻辑，或者依赖完全重置
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));  // 间隔2秒
    }
    
    // 测试结果统计
    float success_rate = (float)success_count / total_attempts * 100.0f;
    ESP_LOGI(TAG, "=== Test Results ===");
    ESP_LOGI(TAG, "Total attempts: %d", total_attempts);
    ESP_LOGI(TAG, "Successful: %d", success_count);
    ESP_LOGI(TAG, "Failed: %d", total_attempts - success_count);
    ESP_LOGI(TAG, "Success rate: %.1f%%", success_rate);
    
    if (success_rate >= 80.0f) {
        ESP_LOGI(TAG, "🎉 EXCELLENT: Connection is very stable!");
    } else if (success_rate >= 60.0f) {
        ESP_LOGW(TAG, "⚠️  GOOD: Connection is mostly stable, minor improvements needed");
    } else {
        ESP_LOGE(TAG, "🚨 POOR: Connection is unstable, further debugging required");
        ESP_LOGE(TAG, "Suggestion: Run wifi_diagnose() to check network availability");
    }
    
    ESP_LOGI(TAG, "=== WiFi Stability Test Completed ===");
}

// 连续监控任务
void wifi_continuous_monitor_task(void *pvParameters)
{
    int check_interval = 10;  // 10秒检查一次
    int disconnection_count = 0;
    int reconnection_attempts = 0;
    
    ESP_LOGI(TAG, "WiFi continuous monitor started (checking every %d seconds)", check_interval);
    
    while (1) {
        if (!wifi_is_connected()) {
            disconnection_count++;
            ESP_LOGW(TAG, "WiFi disconnected (count: %d), attempting reconnection...", disconnection_count);
            
            wifi_smart_reconnect();
            reconnection_attempts++;
            
            // 等待重连结果
            vTaskDelay(pdMS_TO_TICKS(10000));  // 等待10秒查看重连结果
            
            if (wifi_is_connected()) {
                ESP_LOGI(TAG, "✅ Reconnection successful! Connected to: %s", wifi_get_current_ssid());
            } else {
                ESP_LOGE(TAG, "❌ Reconnection failed, will try again in next cycle");
            }
        } else {
            // 连接正常，打印状态信息
            if (disconnection_count > 0) {
                ESP_LOGI(TAG, "📊 Status: Connected to %s (Total disconnections: %d, Reconnection attempts: %d)", 
                         wifi_get_current_ssid(), disconnection_count, reconnection_attempts);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval * 1000));
    }
}

// 启动连续监控
void start_wifi_monitor(void)
{
    xTaskCreate(wifi_continuous_monitor_task, 
                "wifi_monitor", 
                3072, 
                NULL, 
                3, 
                NULL);
    
    ESP_LOGI(TAG, "WiFi continuous monitor task started");
}
