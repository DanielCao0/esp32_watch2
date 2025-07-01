/**
 * @file button_test.c
 * @brief BOOT按钮测试和演示代码
 * 
 * 这个文件展示了如何使用优化后的按钮驱动功能
 */

#include "lvgl_button.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BTN_TEST";

// 自定义按钮事件处理函数示例
static void custom_short_press_handler(void)
{
    ESP_LOGI(TAG, "Custom short press handler called!");
    // 在这里添加你的短按逻辑
}

static void custom_long_press_handler(void)
{
    ESP_LOGI(TAG, "Custom long press handler called!");
    // 在这里添加你的长按逻辑
}

static void custom_double_click_handler(void)
{
    ESP_LOGI(TAG, "Custom double click handler called!");
    // 在这里添加你的双击逻辑
}

// 按钮状态监控任务
static void button_monitor_task(void *pvParameter)
{
    button_stats_t stats;
    static button_state_t last_state = BTN_STATE_IDLE;
    
    ESP_LOGI(TAG, "Button monitor task started");
    
    while (1) {
        get_button_statistics(&stats);
        
        // 只在状态变化时打印
        if (stats.current_state != last_state) {
            ESP_LOGI(TAG, "Button state changed: %s -> %s", 
                    get_button_state_string(),
                    stats.current_state == BTN_STATE_IDLE ? "IDLE" :
                    stats.current_state == BTN_STATE_PRESSED ? "PRESSED" :
                    stats.current_state == BTN_STATE_HELD ? "HELD" : "RELEASED");
            last_state = stats.current_state;
        }
        
        // 如果按钮正在被按下，显示持续时间
        if (stats.last_press_duration > 0) {
            ESP_LOGD(TAG, "Button press duration: %lu ms", stats.last_press_duration);
        }
        
        // 检查双击等待状态
        if (stats.is_waiting_double_click) {
            ESP_LOGD(TAG, "Waiting for potential double click...");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms监控间隔
    }
}

/**
 * @brief 初始化按钮测试
 * 这个函数演示了如何自定义按钮行为
 */
void init_button_test(void)
{
    ESP_LOGI(TAG, "Initializing button test...");
    
    // 首先初始化按钮（使用默认配置）
    init_boot_btn();
    
    // 等待按钮初始化完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 检查按钮任务是否正在运行
    if (is_button_task_running()) {
        ESP_LOGI(TAG, "Button task is running successfully");
    } else {
        ESP_LOGE(TAG, "Button task is not running!");
        return;
    }
    
    // 可选：注册自定义回调函数来覆盖默认行为
    // button_register_short_press_cb(custom_short_press_handler);
    // button_register_long_press_cb(custom_long_press_handler);
    // button_register_double_click_cb(custom_double_click_handler);
    
    // 创建按钮状态监控任务（可选，用于调试）
    xTaskCreate(
        button_monitor_task,
        "btn_monitor",
        2048,
        NULL,
        3, // 较低优先级
        NULL
    );
    
    ESP_LOGI(TAG, "Button test initialized successfully");
    ESP_LOGI(TAG, "Try pressing the BOOT button:");
    ESP_LOGI(TAG, "  - Short press: Show honeycomb menu");
    ESP_LOGI(TAG, "  - Long press (1s+): Reset menu position");
    ESP_LOGI(TAG, "  - Double click: Return to main screen");
}

/**
 * @brief 打印按钮状态信息（调试用）
 */
void print_button_status(void)
{
    button_stats_t stats;
    get_button_statistics(&stats);
    
    ESP_LOGI(TAG, "=== Button Status ===");
    ESP_LOGI(TAG, "Current state: %s", get_button_state_string());
    ESP_LOGI(TAG, "Waiting for double click: %s", stats.is_waiting_double_click ? "YES" : "NO");
    ESP_LOGI(TAG, "Current press duration: %lu ms", stats.last_press_duration);
    ESP_LOGI(TAG, "Task running: %s", is_button_task_running() ? "YES" : "NO");
    ESP_LOGI(TAG, "==================");
}
