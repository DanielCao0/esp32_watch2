#include "hardware_rtc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "soc/rtc.h"
#include "soc/rtc_cntl_reg.h"
#include "hal/rtc_hal.h"
#include "esp_private/rtc_clk.h"
#include "driver/rtc_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_lock.h"
#include "ui.h"
#include "ui/screens/ui_Screen1.h"
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hardware_rtc";

// RTC状态变量
static bool rtc_initialized = false;
static hardware_rtc_status_t rtc_status = HW_RTC_STATUS_NOT_SET;
static uint64_t boot_time_us = 0;
static uint64_t last_sync_time_us = 0;
static int global_timezone_offset_hours = 8;  // 默认北京时间 UTC+8

// LVGL时间更新状态变量
static int last_minute = -1;  // 记录上次的分钟数，用于检测分钟变化
static bool lvgl_update_enabled = false;

// 星期几名称
static const char* weekday_names_cn[] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

static const char* weekday_names_en[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// LVGL时间更新函数（只在分钟变化时更新）
static void update_lvgl_time_if_changed(void)
{
    if (!lvgl_update_enabled) {
        return;
    }
    
    // 获取当前RTC时间
    hardware_rtc_time_t rtc_time;
    if (hardware_rtc_get_time(&rtc_time) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get RTC time for LVGL update");
        return;
    }
    
    // 打印当前获取的时间（用于调试）
    // ESP_LOGI(TAG, "Current RTC time: %04d-%02d-%02d %02d:%02d:%02d (weekday=%d)",
    //          rtc_time.year, rtc_time.month, rtc_time.day,
    //          rtc_time.hour, rtc_time.minute, rtc_time.second, rtc_time.weekday);


    
    // 检查分钟是否变化
    if (last_minute != rtc_time.minute) {
        last_minute = rtc_time.minute;
        
        // 格式化小时和分钟字符串
        char hour_str[8];
        char minute_str[8];
        
        snprintf(hour_str, sizeof(hour_str), "%02d", rtc_time.hour);
        snprintf(minute_str, sizeof(minute_str), "%02d", rtc_time.minute);
        
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
}

/**
 * @brief 轮询检查并更新LVGL时间显示（主函数循环调用）
 */
void hardware_rtc_poll_update_lvgl(void)
{
    // 检查RTC是否已初始化
    if (!rtc_initialized || !lvgl_update_enabled) {
        return;
    }
    
    // 只在分钟变化时更新LVGL界面
    update_lvgl_time_if_changed();
}

/**
 * @brief 初始化硬件RTC
 */
esp_err_t hardware_rtc_init(void)
{
    ESP_LOGI(TAG, "Initializing hardware RTC");
    
    if (rtc_initialized) {
        ESP_LOGW(TAG, "Hardware RTC already initialized");
        return ESP_OK;
    }
    
    // 1. 初始化RTC时钟源和精度
    ESP_LOGI(TAG, "Configuring RTC clock source...");
    
    // 启用内部RC振荡器作为RTC时钟源（可选：外部32kHz晶振）
    rtc_clk_slow_freq_set(RTC_SLOW_FREQ_RTC);
    
    // 2. 配置RTC域电源
    ESP_LOGI(TAG, "Configuring RTC domain power...");
    
    // 启用RTC域的电源保持（深度睡眠时保持RTC运行）
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    // 注意：ESP32-S3的内存域配置可能不同，使用通用配置
#ifdef ESP_PD_DOMAIN_RTC_SLOW_MEM
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
#endif
#ifdef ESP_PD_DOMAIN_RTC_FAST_MEM  
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
#endif
    
    // 3. 初始化RTC硬件寄存器
    ESP_LOGI(TAG, "Initializing RTC registers...");
    
    // 清除RTC唤醒源（避免意外唤醒）
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    
    // 4. 配置系统时区
    ESP_LOGI(TAG, "Setting system timezone to UTC%+d...", global_timezone_offset_hours);
    
    // 设置时区环境变量
    char timezone_str[32];
    if (global_timezone_offset_hours >= 0) {
        snprintf(timezone_str, sizeof(timezone_str), "UTC-%d", global_timezone_offset_hours);
    } else {
        snprintf(timezone_str, sizeof(timezone_str), "UTC+%d", -global_timezone_offset_hours);
    }
    setenv("TZ", timezone_str, 1);
    tzset();
    ESP_LOGI(TAG, "System timezone set to: %s", timezone_str);
    
    // 5. 记录启动时间和硬件状态
    boot_time_us = esp_timer_get_time();
    
    // 获取RTC时钟频率信息
    uint32_t rtc_clk_freq = rtc_clk_slow_freq_get_hz();
    ESP_LOGI(TAG, "RTC slow clock frequency: %lu Hz", rtc_clk_freq);
    
    // 5. 检查系统时间是否已设置
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    if (tv.tv_sec > 946684800) {  // 2000年1月1日的时间戳
        rtc_status = HW_RTC_STATUS_RUNNING;        
    } else {
        rtc_status = HW_RTC_STATUS_NOT_SET;
        ESP_LOGI(TAG, "System time not set, RTC waiting for time sync");
    }
    
    // 6. 验证RTC是否正常工作
    uint64_t rtc_time_before = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(10));  // 等待10ms
    uint64_t rtc_time_after = esp_timer_get_time();
    
    if (rtc_time_after > rtc_time_before) {
        ESP_LOGI(TAG, "RTC timer verification passed (delta: %llu us)", 
                 rtc_time_after - rtc_time_before);
    } else {
        ESP_LOGW(TAG, "RTC timer verification failed - clock may be unstable");
    }
    
    rtc_initialized = true;
    
    ESP_LOGI(TAG, "Hardware RTC initialized successfully");
    ESP_LOGI(TAG, "  Boot time: %llu us", boot_time_us);
    ESP_LOGI(TAG, "  RTC clock freq: %lu Hz", rtc_clk_freq);
    ESP_LOGI(TAG, "  Timezone offset: UTC%+d", global_timezone_offset_hours);
    ESP_LOGI(TAG, "  RTC status: %s", 
             (rtc_status == HW_RTC_STATUS_RUNNING) ? "RUNNING" : "NOT_SET");
    ESP_LOGI(TAG, "  Power config: RTC domain enabled for deep sleep");
    
    // 启用LVGL更新（无需定时器，主循环调用）
    lvgl_update_enabled = true;
    ESP_LOGI(TAG, "LVGL time update enabled for main loop polling");
    
    return ESP_OK;
}

/**
 * @brief 设置RTC时间
 */
esp_err_t hardware_rtc_set_time(const hardware_rtc_time_t *rtc_time)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (rtc_time == NULL) {
        ESP_LOGE(TAG, "RTC time pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证时间范围
    if (rtc_time->year < 2000 || rtc_time->year > 2100 ||
        rtc_time->month < 1 || rtc_time->month > 12 ||
        rtc_time->day < 1 || rtc_time->day > 31 ||
        rtc_time->hour < 0 || rtc_time->hour > 23 ||
        rtc_time->minute < 0 || rtc_time->minute > 59 ||
        rtc_time->second < 0 || rtc_time->second > 59) {
        ESP_LOGE(TAG, "Invalid time values");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 转换为tm结构（本地时间）
    struct tm timeinfo = {0};
    timeinfo.tm_year = rtc_time->year - 1900;  // tm_year从1900年开始计算
    timeinfo.tm_mon = rtc_time->month - 1;     // tm_mon从0开始计算
    timeinfo.tm_mday = rtc_time->day;
    timeinfo.tm_hour = rtc_time->hour;
    timeinfo.tm_min = rtc_time->minute;
    timeinfo.tm_sec = rtc_time->second;
    timeinfo.tm_wday = rtc_time->weekday;
    timeinfo.tm_isdst = -1;  // 让系统自动处理夏令时
    
    // 转换为UTC时间戳（mktime假设输入是本地时间）
    time_t timestamp = mktime(&timeinfo);
    if (timestamp == -1) {
        ESP_LOGE(TAG, "Failed to convert time to timestamp");
        return ESP_FAIL;
    }
    
    // 由于ESP32的时区设置，mktime已经正确处理了时区转换
    // 不需要手动调整时区偏移
    
    // 设置系统时间（这会自动设置RTC）
    struct timeval tv = {
        .tv_sec = timestamp,
        .tv_usec = 0
    };
    
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to set system time");
        return ESP_FAIL;
    }
    
    rtc_status = HW_RTC_STATUS_RUNNING;
    last_sync_time_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "RTC time set successfully: %04d-%02d-%02d %02d:%02d:%02d",
             rtc_time->year, rtc_time->month, rtc_time->day,
             rtc_time->hour, rtc_time->minute, rtc_time->second);
    
    return ESP_OK;
}

/**
 * @brief 获取RTC时间
 */
esp_err_t hardware_rtc_get_time(hardware_rtc_time_t *rtc_time)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (rtc_time == NULL) {
        ESP_LOGE(TAG, "RTC time pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取当前系统时间（UTC）
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // 使用系统的本地时间转换函数
    struct tm *timeinfo = localtime(&tv.tv_sec);
    if (timeinfo == NULL) {
        ESP_LOGE(TAG, "Failed to convert timestamp to local time");
        return ESP_FAIL;
    }
    
    // 填充RTC时间结构
    rtc_time->year = timeinfo->tm_year + 1900;
    rtc_time->month = timeinfo->tm_mon + 1;
    rtc_time->day = timeinfo->tm_mday;
    rtc_time->hour = timeinfo->tm_hour;
    rtc_time->minute = timeinfo->tm_min;
    rtc_time->second = timeinfo->tm_sec;
    rtc_time->weekday = timeinfo->tm_wday;
    
    // 调试日志：显示UTC时间和本地时间
    // struct tm *utc_timeinfo = gmtime(&tv.tv_sec);
    // ESP_LOGI(TAG, "RTC time retrieved - UTC: %02d:%02d:%02d, Local(+%d): %02d:%02d:%02d",
    //          utc_timeinfo->tm_hour, utc_timeinfo->tm_min, utc_timeinfo->tm_sec,
    //          global_timezone_offset_hours,
    //          rtc_time->hour, rtc_time->minute, rtc_time->second);
    
    return ESP_OK;
}

/**
 * @brief 从NTP时间同步到RTC
 */
esp_err_t hardware_rtc_sync_from_ntp(uint64_t ntp_timestamp_us)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    rtc_status = HW_RTC_STATUS_SYNC_NTP;
    
    // 转换为秒
    time_t ntp_timestamp = ntp_timestamp_us / 1000000;
    
    // 设置系统时间
    struct timeval tv = {
        .tv_sec = ntp_timestamp,
        .tv_usec = ntp_timestamp_us % 1000000
    };
    
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to sync time from NTP");
        rtc_status = HW_RTC_STATUS_ERROR;
        return ESP_FAIL;
    }
    
    rtc_status = HW_RTC_STATUS_RUNNING;
    last_sync_time_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "RTC synced from NTP successfully");
    ESP_LOGI(TAG, "  NTP timestamp: %llu us", ntp_timestamp_us);
    ESP_LOGI(TAG, "  Local time: %ld", tv.tv_sec + global_timezone_offset_hours * 3600);
    
    return ESP_OK;
}

/**
 * @brief 从系统时间同步到RTC
 */
esp_err_t hardware_rtc_sync_from_system(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // ESP32的RTC会自动与系统时间保持同步
    // 这里主要是更新状态和记录同步时间
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    if (tv.tv_sec > 946684800) {  // 检查时间是否合理
        rtc_status = HW_RTC_STATUS_RUNNING;
        last_sync_time_us = esp_timer_get_time();
        
        ESP_LOGI(TAG, "RTC synced from system time");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "System time appears invalid, sync failed");
        return ESP_FAIL;
    }
}

/**
 * @brief 将RTC时间同步到系统时间
 */
esp_err_t hardware_rtc_sync_to_system(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // ESP32的RTC和系统时间是同一个时钟源
    // 这个函数主要用于状态确认
    
    ESP_LOGI(TAG, "RTC and system time are already synchronized");
    return ESP_OK;
}

/**
 * @brief 获取RTC状态信息
 */
esp_err_t hardware_rtc_get_info(hardware_rtc_info_t *info)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (info == NULL) {
        ESP_LOGE(TAG, "Info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    info->boot_time_us = boot_time_us;
    info->last_sync_time_us = last_sync_time_us;
    info->is_time_set = (rtc_status == HW_RTC_STATUS_RUNNING);
    info->status = rtc_status;
    
    return ESP_OK;
}

/**
 * @brief 格式化RTC时间为字符串
 */
esp_err_t hardware_rtc_format_time(const hardware_rtc_time_t *rtc_time, 
                                   char *buffer, size_t buffer_size, 
                                   const char *format)
{
    if (rtc_time == NULL || buffer == NULL || format == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for time formatting");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strcmp(format, "datetime") == 0) {
        snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d",
                rtc_time->year, rtc_time->month, rtc_time->day,
                rtc_time->hour, rtc_time->minute, rtc_time->second);
    } else if (strcmp(format, "time") == 0) {
        snprintf(buffer, buffer_size, "%02d:%02d:%02d",
                rtc_time->hour, rtc_time->minute, rtc_time->second);
    } else if (strcmp(format, "date") == 0) {
        snprintf(buffer, buffer_size, "%04d-%02d-%02d",
                rtc_time->year, rtc_time->month, rtc_time->day);
    } else if (strcmp(format, "iso8601") == 0) {
        snprintf(buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02d%+03d:00",
                rtc_time->year, rtc_time->month, rtc_time->day,
                rtc_time->hour, rtc_time->minute, rtc_time->second,
                global_timezone_offset_hours);
    } else if (strcmp(format, "chinese") == 0) {
        snprintf(buffer, buffer_size, "%04d年%02d月%02d日 %s %02d:%02d:%02d",
                rtc_time->year, rtc_time->month, rtc_time->day,
                hardware_rtc_get_weekday_name_cn(rtc_time->weekday),
                rtc_time->hour, rtc_time->minute, rtc_time->second);
    } else {
        ESP_LOGE(TAG, "Unknown format: %s", format);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/**
 * @brief 获取RTC运行时长（秒）
 */
uint64_t hardware_rtc_get_uptime_seconds(void)
{
    if (!rtc_initialized) {
        return 0;
    }
    
    uint64_t current_time = esp_timer_get_time();
    return (current_time - boot_time_us) / 1000000;
}

/**
 * @brief 获取当前时间戳（Unix时间戳，秒）
 */
time_t hardware_rtc_get_timestamp(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

/**
 * @brief 设置时区偏移
 */
esp_err_t hardware_rtc_set_timezone(int timezone_offset_hours)
{
    if (timezone_offset_hours < -12 || timezone_offset_hours > 12) {
        ESP_LOGE(TAG, "Invalid timezone offset: %d", timezone_offset_hours);
        return ESP_ERR_INVALID_ARG;
    }
    
    global_timezone_offset_hours = timezone_offset_hours;
    
    ESP_LOGI(TAG, "Timezone set to UTC%+d", global_timezone_offset_hours);
    return ESP_OK;
}

/**
 * @brief 检查是否为闰年
 */
bool hardware_rtc_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief 计算星期几（使用基姆拉尔森公式）
 */
int hardware_rtc_calculate_weekday(int year, int month, int day)
{
    if (month < 3) {
        month += 12;
        year--;
    }
    
    int century = year / 100;
    year = year % 100;
    
    int weekday = (day + (13 * (month + 1)) / 5 + year + year / 4 + century / 4 - 2 * century) % 7;
    
    // 转换为0=周日的格式
    return (weekday + 6) % 7;
}

/**
 * @brief 获取星期几的中文名称
 */
const char* hardware_rtc_get_weekday_name_cn(int weekday)
{
    if (weekday < 0 || weekday > 6) {
        return "未知";
    }
    return weekday_names_cn[weekday];
}

/**
 * @brief 获取星期几的英文名称
 */
const char* hardware_rtc_get_weekday_name_en(int weekday)
{
    if (weekday < 0 || weekday > 6) {
        return "Unknown";
    }
    return weekday_names_en[weekday];
}

/**
 * @brief 显示硬件RTC详细状态信息
 */
esp_err_t hardware_rtc_show_status(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "=== Hardware RTC Status ===");
    
    // 1. 基本状态信息
    ESP_LOGI(TAG, "RTC Initialized: %s", rtc_initialized ? "YES" : "NO");
    ESP_LOGI(TAG, "RTC Status: %s", 
             (rtc_status == HW_RTC_STATUS_RUNNING) ? "RUNNING" :
             (rtc_status == HW_RTC_STATUS_NOT_SET) ? "NOT_SET" :
             (rtc_status == HW_RTC_STATUS_SYNC_NTP) ? "SYNC_NTP" : "ERROR");
    
    // 2. 时区和时间信息
    ESP_LOGI(TAG, "Timezone: UTC%+d", global_timezone_offset_hours);
    
    // 3. 运行时信息
    uint64_t current_time_us = esp_timer_get_time();
    uint64_t uptime_seconds = (current_time_us - boot_time_us) / 1000000;
    ESP_LOGI(TAG, "Boot time: %llu us", boot_time_us);
    ESP_LOGI(TAG, "Current time: %llu us", current_time_us);
    ESP_LOGI(TAG, "Uptime: %llu seconds (%llu:%02llu:%02llu)", 
             uptime_seconds, uptime_seconds/3600, (uptime_seconds%3600)/60, uptime_seconds%60);
    
    // 4. 硬件RTC时钟信息
    uint32_t rtc_clk_freq = rtc_clk_slow_freq_get_hz();
    ESP_LOGI(TAG, "RTC Clock Source: %s", 
             (rtc_clk_slow_freq_get() == RTC_SLOW_FREQ_RTC) ? "Internal RC" :
             (rtc_clk_slow_freq_get() == RTC_SLOW_FREQ_32K_XTAL) ? "External 32kHz" : "Other");
    ESP_LOGI(TAG, "RTC Clock Frequency: %lu Hz", rtc_clk_freq);
    
    // 5. 系统时间信息
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t timestamp = tv.tv_sec;
    struct tm *timeinfo = localtime(&timestamp);
    
    if (timeinfo != NULL) {
        ESP_LOGI(TAG, "System Time: %04d-%02d-%02d %02d:%02d:%02d.%06ld",
                 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                 timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, tv.tv_usec);
        ESP_LOGI(TAG, "Unix Timestamp: %ld", timestamp);
    } else {
        ESP_LOGW(TAG, "System Time: Not available");
    }
    
    // 6. 同步信息
    if (last_sync_time_us > 0) {
        uint64_t sync_age_seconds = (current_time_us - last_sync_time_us) / 1000000;
        ESP_LOGI(TAG, "Last Sync: %llu us ago (%llu seconds)", 
                 current_time_us - last_sync_time_us, sync_age_seconds);
    } else {
        ESP_LOGI(TAG, "Last Sync: Never");
    }
    
    ESP_LOGI(TAG, "========================");
    
    return ESP_OK;
}

/**
 * @brief 运行RTC演示程序
 */
esp_err_t hardware_rtc_demo(void)
{
    ESP_LOGI(TAG, "=== Hardware RTC Demo ===");
    
    // 1. 显示RTC状态
    esp_err_t ret = hardware_rtc_show_status();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to show RTC status");
        return ret;
    }
    
    // 2. 获取当前RTC时间
    hardware_rtc_time_t rtc_time;
    ret = hardware_rtc_get_time(&rtc_time);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current RTC Time:");
        ESP_LOGI(TAG, "  Date: %04d-%02d-%02d (%s)",
                 rtc_time.year, rtc_time.month, rtc_time.day,
                 hardware_rtc_get_weekday_name_en(rtc_time.weekday));
        ESP_LOGI(TAG, "  Time: %02d:%02d:%02d",
                 rtc_time.hour, rtc_time.minute, rtc_time.second);
        
        // 3. 格式化时间演示
        char time_str[64];
        
        // 时间格式
        hardware_rtc_format_time(&rtc_time, time_str, sizeof(time_str), "time");
        ESP_LOGI(TAG, "  Formatted Time: %s", time_str);
        
        // 日期格式
        hardware_rtc_format_time(&rtc_time, time_str, sizeof(time_str), "date");
        ESP_LOGI(TAG, "  Formatted Date: %s", time_str);
        
        // ISO8601格式
        hardware_rtc_format_time(&rtc_time, time_str, sizeof(time_str), "iso8601");
        ESP_LOGI(TAG, "  ISO8601: %s", time_str);
        
        // 完整日期时间格式
        hardware_rtc_format_time(&rtc_time, time_str, sizeof(time_str), "datetime");
        ESP_LOGI(TAG, "  DateTime: %s", time_str);
        
    } else {
        ESP_LOGW(TAG, "Failed to get RTC time (error: %d)", ret);
    }
    
    // 4. 显示时间戳
    time_t timestamp = hardware_rtc_get_timestamp();
    ESP_LOGI(TAG, "Unix Timestamp: %ld", timestamp);
    
    // 5. 显示运行时长
    uint64_t uptime = hardware_rtc_get_uptime_seconds();
    ESP_LOGI(TAG, "System Uptime: %llu seconds", uptime);
    
    // 6. 年份检查演示
    ESP_LOGI(TAG, "Year Check:");
    for (int year = 2020; year <= 2025; year++) {
        ESP_LOGI(TAG, "  %d: %s", year, 
                 hardware_rtc_is_leap_year(year) ? "Leap Year" : "Normal Year");
    }
    
    ESP_LOGI(TAG, "=====================");
    
    return ESP_OK;
}

/**
 * @brief 启用LVGL时间显示更新
 */
esp_err_t hardware_rtc_enable_lvgl_update(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    lvgl_update_enabled = true;
    ESP_LOGI(TAG, "LVGL time update enabled");
    
    // 立即更新一次显示
    update_lvgl_time_if_changed();
    
    return ESP_OK;
}

/**
 * @brief 禁用LVGL时间显示更新
 */
esp_err_t hardware_rtc_disable_lvgl_update(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    lvgl_update_enabled = false;
    ESP_LOGI(TAG, "LVGL time update disabled");
    
    return ESP_OK;
}

/**
 * @brief 手动更新LVGL时间显示
 */
esp_err_t hardware_rtc_update_lvgl_display(void)
{
    if (!rtc_initialized) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 强制更新显示（忽略分钟变化检查）
    hardware_rtc_time_t rtc_time;
    if (hardware_rtc_get_time(&rtc_time) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get RTC time for manual update");
        return ESP_FAIL;
    }
    
    // 格式化小时和分钟字符串
    char hour_str[8];
    char minute_str[8];
    
    snprintf(hour_str, sizeof(hour_str), "%02d", rtc_time.hour);
    snprintf(minute_str, sizeof(minute_str), "%02d", rtc_time.minute);
    
    // 更新LVGL对象（使用互斥锁保护）
    if (lvgl_lock(50)) {
        if (ui_hour != NULL) {
            lv_label_set_text(ui_hour, hour_str);
        }
        if (ui_minutes != NULL) {
            lv_label_set_text(ui_minutes, minute_str);
        }
        lvgl_unlock();
        
        ESP_LOGI(TAG, "LVGL time display manually updated: %s:%s", hour_str, minute_str);
        
        // 更新last_minute以保持状态一致性
        last_minute = rtc_time.minute;
        
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to acquire LVGL lock for manual update");
        return ESP_FAIL;
    }
}

/**
 * @brief 反初始化硬件RTC（清理资源）
 */
esp_err_t hardware_rtc_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing hardware RTC");
    
    // 禁用LVGL更新
    lvgl_update_enabled = false;
    
    // 重置状态变量
    rtc_initialized = false;
    rtc_status = HW_RTC_STATUS_NOT_SET;
    last_minute = -1;
    
    ESP_LOGI(TAG, "Hardware RTC deinitialized successfully");
    
    return ESP_OK;
}
