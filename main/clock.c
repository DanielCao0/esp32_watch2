#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "clock.h"

static const char *TAG = "clock";

// 时间同步定时器
static TimerHandle_t time_sync_timer = NULL;
static bool time_sync_initialized = false;

// 判断WiFi是否已连接
bool is_wifi_connected()
{
    wifi_ap_record_t ap_info;
    return esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
}

// 获取北京时间（东八区）
void obtain_beijing_time()
{
    if (!is_wifi_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, unable to get time");
        return;
    }

    ESP_LOGI(TAG, "Synchronizing NTP time...");
    
    // 如果SNTP已经初始化，先停止
    if (time_sync_initialized) {
        esp_sntp_stop();
        time_sync_initialized = false;
    }
    
    // 设置时区为东八区（在SNTP初始化前设置）
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 配置SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp1.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");  // 备用服务器
    esp_sntp_init();
    time_sync_initialized = true;

    // 等待时间同步
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;  // 增加重试次数
    
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // 减少等待时间
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGE(TAG, "NTP time synchronization failed");
        return;
    }

    ESP_LOGI(TAG, "Beijing time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

// 时间同步定时器回调函数
static void time_sync_timer_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Time sync timer triggered");
    obtain_beijing_time();
}

// 初始化时钟系统
void clock_init(void)
{
    ESP_LOGI(TAG, "Initializing clock system");
    
    // 设置默认时区为东八区
    setenv("TZ", "CST-8", 1);
    tzset();
    
    // 创建15分钟定时器（15 * 60 * 1000ms = 900000ms）
    time_sync_timer = xTimerCreate("time_sync",
                                   pdMS_TO_TICKS(15 * 60 * 1000),  // 15分钟
                                   pdTRUE,  // 自动重载
                                   NULL,
                                   time_sync_timer_callback);
    
    if (time_sync_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create time sync timer");
        return;
    }
    
    // 立即执行一次时间同步
    obtain_beijing_time();
    
    // 启动定时器
    if (xTimerStart(time_sync_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start time sync timer");
    } else {
        ESP_LOGI(TAG, "Time sync timer started (15 minutes interval)");
    }
}

// 停止时钟系统
void clock_deinit(void)
{
    if (time_sync_timer != NULL) {
        xTimerStop(time_sync_timer, 0);
        xTimerDelete(time_sync_timer, 0);
        time_sync_timer = NULL;
        ESP_LOGI(TAG, "Time sync timer stopped and deleted");
    }
    
    if (time_sync_initialized) {
        esp_sntp_stop();
        time_sync_initialized = false;
        ESP_LOGI(TAG, "SNTP stopped");
    }
}

// 示例调用（保持兼容性）
void app_get_beijing_time()
{
    if (is_wifi_connected()) {
        obtain_beijing_time();
    } else {
        ESP_LOGW(TAG, "WiFi not connected");
    }
}

// 获取当前的本地时间信息
bool get_current_time(struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return false;
    }
    
    time_t now;
    time(&now);
    localtime_r(&now, timeinfo);
    
    // 检查时间是否有效（年份大于2016）
    return (timeinfo->tm_year >= (2016 - 1900));
}

// 格式化时间字符串
void format_time_string(const struct tm *timeinfo, char *time_str, size_t max_len, const char *format)
{
    if (timeinfo == NULL || time_str == NULL || format == NULL) {
        return;
    }
    
    strftime(time_str, max_len, format, timeinfo);
}

// 获取格式化的当前时间字符串
bool get_formatted_time(char *time_str, size_t max_len, const char *format)
{
    if (time_str == NULL || format == NULL) {
        return false;
    }
    
    struct tm timeinfo;
    if (!get_current_time(&timeinfo)) {
        // 时间无效，返回默认字符串
        snprintf(time_str, max_len, "--:--");
        return false;
    }
    
    format_time_string(&timeinfo, time_str, max_len, format);
    return true;
}