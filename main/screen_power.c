#include "screen_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "screen_power";

// 屏幕电源管理状态
typedef struct {
    bool is_awake;                  // 当前屏幕状态
    uint64_t last_touch_time;       // 最后一次触摸时间（微秒）
    uint32_t sleep_timeout_ms;      // 息屏超时时间（毫秒）
    esp_lcd_panel_handle_t panel;   // LCD面板句柄
    SemaphoreHandle_t mutex;        // 保护状态的互斥锁
} screen_power_state_t;

static screen_power_state_t g_screen_power = {
    .is_awake = true,
    .last_touch_time = 0,
    .sleep_timeout_ms = 30000,      // 默认15秒
    .panel = NULL,
    .mutex = NULL
};

// 外部变量声明（从主文件获取LCD面板句柄）
// 注意：实际上我们通过函数参数传递，不需要外部变量

esp_err_t screen_power_init(void)
{
    ESP_LOGI(TAG, "Initializing screen power management");
    
    // 创建互斥锁
    g_screen_power.mutex = xSemaphoreCreateMutex();
    if (g_screen_power.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 获取LCD面板句柄
    g_screen_power.panel = NULL; // 初始化为NULL，稍后通过函数设置
    ESP_LOGI(TAG, "Screen power management will be configured after LCD panel initialization");
    
    // 初始化时间戳
    g_screen_power.last_touch_time = esp_timer_get_time();
    g_screen_power.is_awake = true;
    
    ESP_LOGI(TAG, "Screen power management initialized (timeout: %lu ms)", g_screen_power.sleep_timeout_ms);
    return ESP_OK;
}

void screen_power_touch_activity(void)
{
    if (g_screen_power.mutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(g_screen_power.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint64_t current_time = esp_timer_get_time();
        g_screen_power.last_touch_time = current_time;
        
        // 如果屏幕当前是息屏状态，立即唤醒
        if (!g_screen_power.is_awake) {
            ESP_LOGI(TAG, "Touch detected, waking up screen");
            screen_power_wake_up();
        } else {
            ESP_LOGD(TAG, "Touch activity detected, resetting sleep timer");
        }
        
        xSemaphoreGive(g_screen_power.mutex);
    }
}

void screen_power_check_sleep(void)
{
    if (g_screen_power.mutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(g_screen_power.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // 只有在屏幕唤醒状态下才检查是否需要息屏
        if (g_screen_power.is_awake) {
            uint64_t current_time = esp_timer_get_time();
            uint64_t time_since_touch = current_time - g_screen_power.last_touch_time;
            
            // 转换为毫秒进行比较
            if (time_since_touch >= (g_screen_power.sleep_timeout_ms * 1000ULL)) {
                ESP_LOGI(TAG, "No touch for %lu ms, putting screen to sleep", 
                         (uint32_t)(time_since_touch / 1000));
                screen_power_sleep();
            }
        }
        
        xSemaphoreGive(g_screen_power.mutex);
    }
}

void screen_power_wake_up(void)
{
    if (g_screen_power.panel == NULL) {
        ESP_LOGW(TAG, "LCD panel not available, cannot wake up screen");
        return;
    }
    
    if (!g_screen_power.is_awake) {
        ESP_LOGI(TAG, "Waking up screen");
        
        // 打开LCD显示
        esp_err_t ret = esp_lcd_panel_disp_on_off(g_screen_power.panel, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
        } else {
            g_screen_power.is_awake = true;
            ESP_LOGI(TAG, "Screen woke up successfully");
        }
    }
    
    // 更新触摸时间戳
    g_screen_power.last_touch_time = esp_timer_get_time();
}

void screen_power_sleep(void)
{
    if (g_screen_power.panel == NULL) {
        ESP_LOGW(TAG, "LCD panel not available, cannot put screen to sleep");
        return;
    }
    
    if (g_screen_power.is_awake) {
        ESP_LOGI(TAG, "Putting screen to sleep");
        
        // 关闭LCD显示
        esp_err_t ret = esp_lcd_panel_disp_on_off(g_screen_power.panel, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn off display: %s", esp_err_to_name(ret));
        } else {
            g_screen_power.is_awake = false;
            ESP_LOGI(TAG, "Screen went to sleep successfully");
        }
    }
}

bool screen_power_is_awake(void)
{
    bool awake = false;
    
    if (g_screen_power.mutex != NULL) {
        if (xSemaphoreTake(g_screen_power.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            awake = g_screen_power.is_awake;
            xSemaphoreGive(g_screen_power.mutex);
        }
    }
    
    return awake;
}

void screen_power_set_timeout(uint32_t timeout_seconds)
{
    if (g_screen_power.mutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(g_screen_power.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_screen_power.sleep_timeout_ms = timeout_seconds * 1000;
        ESP_LOGI(TAG, "Screen sleep timeout set to %lu seconds", timeout_seconds);
        xSemaphoreGive(g_screen_power.mutex);
    }
}

// 设置LCD面板句柄的函数（供主文件调用）
void screen_power_set_panel_handle(void* panel)
{
    if (g_screen_power.mutex != NULL) {
        if (xSemaphoreTake(g_screen_power.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_screen_power.panel = (esp_lcd_panel_handle_t)panel;
            ESP_LOGI(TAG, "LCD panel handle set for screen power management");
            xSemaphoreGive(g_screen_power.mutex);
        }
    }
}
