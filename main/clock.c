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
#include "freertos/queue.h"
#include "clock.h"
#include "ui.h"
#include "ui/screens/ui_Screen1.h"
#include "lvgl_lock.h"

static const char *TAG = "clock";

// 时间同步定时器（15分钟）
static TimerHandle_t time_sync_timer = NULL;
static bool time_sync_initialized = false;

// UI更新定时器（每秒更新一次）
static TimerHandle_t ui_update_timer = NULL;

// 时钟事件队列
static QueueHandle_t clock_event_queue = NULL;
#define CLOCK_EVENT_QUEUE_SIZE 10

// 发送时钟事件到队列
static void send_clock_event(clock_event_type_t event_type)
{
    if (clock_event_queue != NULL) {
        clock_event_t event = { .type = event_type };
        
        // 检查是否在中断上下文中
        if (xPortInIsrContext()) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            
            // 从中断中发送事件
            if (xQueueSendFromISR(clock_event_queue, &event, &xHigherPriorityTaskWoken) != pdTRUE) {
                // 队列满了，在ISR中不能使用ESP_LOGW
            }
            
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        } else {
            // 从普通任务中发送事件
            if (xQueueSend(clock_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to send clock event %d to queue", event_type);
            }
        }
    }
}

// 更新LVGL时间显示
static void update_lvgl_time_display(void)
{
    struct tm timeinfo;
    if (!get_current_time(&timeinfo)) {
        // 时间无效，显示默认值
        if (lvgl_lock(50)) {
            if (ui_hour != NULL) {
                lv_label_set_text(ui_hour, "--");
            }
            if (ui_minutes != NULL) {
                lv_label_set_text(ui_minutes, "--");
            }
            lvgl_unlock();
        }
        return;
    }

    // 格式化小时和分钟字符串
    char hour_str[8];
    char minute_str[8];
    
    snprintf(hour_str, sizeof(hour_str), "%02d", timeinfo.tm_hour);
    snprintf(minute_str, sizeof(minute_str), "%02d", timeinfo.tm_min);
    
    // 更新LVGL对象（使用互斥锁保护）
    if (lvgl_lock(50)) {
        if (ui_hour != NULL) {
            lv_label_set_text(ui_hour, hour_str);
        }
        if (ui_minutes != NULL) {
            lv_label_set_text(ui_minutes, minute_str);
        }
        lvgl_unlock();
        
        ESP_LOGI(TAG, "LVGL time display updated: %s:%s", hour_str, minute_str);
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL lock for time display update");
    }
}

// UI更新定时器回调函数（只发送事件）
static void ui_update_timer_callback(TimerHandle_t timer)
{
    send_clock_event(CLOCK_EVENT_UPDATE_UI);
}

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

    // 立即更新LVGL时间显示
    update_lvgl_time_display();       
}

// 时间同步定时器回调函数（只发送事件）
static void time_sync_timer_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Time sync timer triggered - sending event to main task");
    send_clock_event(CLOCK_EVENT_SYNC_TIME);
}

// 初始化时钟系统
void clock_init(void)
{
    ESP_LOGI(TAG, "Initializing clock system");
    
    // 创建时钟事件队列
    clock_event_queue = xQueueCreate(CLOCK_EVENT_QUEUE_SIZE, sizeof(clock_event_t));
    if (clock_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create clock event queue");
        return;
    }
    
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
    
    // 立即发送一次时间同步事件
    send_clock_event(CLOCK_EVENT_SYNC_TIME);
    
    // 启动定时器
    if (xTimerStart(time_sync_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start time sync timer");
    } else {
        ESP_LOGI(TAG, "Time sync timer started (15 minutes interval)");
    }
    
    // 创建UI更新定时器（每秒更新一次）
    ui_update_timer = xTimerCreate("ui_update",
                                   pdMS_TO_TICKS(1000),  // 1秒
                                   pdTRUE,  // 自动重载
                                   NULL,
                                   ui_update_timer_callback);
    
    if (ui_update_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create UI update timer");
        return;
    }
    
    // 启动UI更新定时器
    if (xTimerStart(ui_update_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start UI update timer");
    } else {
        ESP_LOGI(TAG, "UI update timer started (1 second interval)");
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
    
    if (ui_update_timer != NULL) {
        xTimerStop(ui_update_timer, 0);
        xTimerDelete(ui_update_timer, 0);
        ui_update_timer = NULL;
        ESP_LOGI(TAG, "UI update timer stopped and deleted");
    }
    
    if (clock_event_queue != NULL) {
        vQueueDelete(clock_event_queue);
        clock_event_queue = NULL;
        ESP_LOGI(TAG, "Clock event queue deleted");
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

// 手动更新LVGL时间显示（公共API）
void update_lvgl_time_display_manual(void)
{
    update_lvgl_time_display();
}

// 获取时钟事件队列（供主任务使用）
QueueHandle_t get_clock_event_queue(void)
{
    return clock_event_queue;
}

// 处理时钟事件（在主任务中调用）
void handle_clock_event(const clock_event_t *event)
{
    if (event == NULL) {
        ESP_LOGW(TAG, "Clock event pointer is NULL");
        return;
    }
    
    switch (event->type) {
        case CLOCK_EVENT_UPDATE_UI:
            // 更新UI显示
            update_lvgl_time_display();
            break;
            
        case CLOCK_EVENT_SYNC_TIME:
            ESP_LOGI(TAG, "Processing time sync event");
            // 执行时间同步
            obtain_beijing_time();
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown clock event type: %d", event->type);
            break;
    }
}