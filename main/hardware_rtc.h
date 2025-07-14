#ifndef HARDWARE_RTC_H
#define HARDWARE_RTC_H

#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// RTC时间结构体
typedef struct {
    int year;       // 年份 (例如 2025)
    int month;      // 月份 (1-12)
    int day;        // 日期 (1-31)
    int hour;       // 小时 (0-23)
    int minute;     // 分钟 (0-59)
    int second;     // 秒钟 (0-59)
    int weekday;    // 星期 (0=周日, 1=周一, ..., 6=周六)
} hardware_rtc_time_t;

// RTC状态枚举
typedef enum {
    HW_RTC_STATUS_NOT_SET = 0,      // RTC时间未设置
    HW_RTC_STATUS_RUNNING,          // RTC正常运行
    HW_RTC_STATUS_SYNC_NTP,         // 与NTP同步中
    HW_RTC_STATUS_ERROR             // RTC错误
} hardware_rtc_status_t;

// RTC统计信息
typedef struct {
    uint64_t boot_time_us;          // 系统启动时的微秒时间戳
    uint64_t last_sync_time_us;     // 上次同步时的微秒时间戳
    bool is_time_set;               // 是否已设置时间
    hardware_rtc_status_t status;   // RTC状态
} hardware_rtc_info_t;

/**
 * @brief 初始化硬件RTC
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_init(void);

/**
 * @brief 设置RTC时间
 * @param rtc_time RTC时间结构体指针
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_set_time(const hardware_rtc_time_t *rtc_time);

/**
 * @brief 获取RTC时间
 * @param rtc_time 输出RTC时间结构体指针
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_get_time(hardware_rtc_time_t *rtc_time);

/**
 * @brief 从NTP时间同步到RTC
 * @param ntp_timestamp_us NTP时间戳（微秒）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_sync_from_ntp(uint64_t ntp_timestamp_us);

/**
 * @brief 从系统时间同步到RTC
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_sync_from_system(void);

/**
 * @brief 将RTC时间同步到系统时间
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_sync_to_system(void);

/**
 * @brief 获取RTC状态信息
 * @param info 输出RTC信息结构体指针
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_get_info(hardware_rtc_info_t *info);

/**
 * @brief 格式化RTC时间为字符串
 * @param rtc_time RTC时间结构体指针
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param format 格式字符串 (支持 "datetime", "time", "date", "iso8601")
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_format_time(const hardware_rtc_time_t *rtc_time, 
                                   char *buffer, size_t buffer_size, 
                                   const char *format);

/**
 * @brief 获取RTC运行时长（秒）
 * @return RTC运行时长（秒）
 */
uint64_t hardware_rtc_get_uptime_seconds(void);

/**
 * @brief 获取当前时间戳（Unix时间戳，秒）
 * @return Unix时间戳（秒）
 */
time_t hardware_rtc_get_timestamp(void);

/**
 * @brief 设置时区偏移
 * @param timezone_offset_hours 时区偏移小时数 (例如：北京时间 +8)
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_set_timezone(int timezone_offset_hours);

/**
 * @brief 检查是否为闰年
 * @param year 年份
 * @return true 闰年，false 平年
 */
bool hardware_rtc_is_leap_year(int year);

/**
 * @brief 计算星期几
 * @param year 年份
 * @param month 月份 (1-12)
 * @param day 日期 (1-31)
 * @return 星期几 (0=周日, 1=周一, ..., 6=周六)
 */
int hardware_rtc_calculate_weekday(int year, int month, int day);

/**
 * @brief 获取星期几的中文名称
 * @param weekday 星期几 (0-6)
 * @return 星期几的中文字符串
 */
const char* hardware_rtc_get_weekday_name_cn(int weekday);

/**
 * @brief 获取星期几的英文名称
 * @param weekday 星期几 (0-6)
 * @return 星期几的英文字符串
 */
const char* hardware_rtc_get_weekday_name_en(int weekday);

/**
 * @brief 显示硬件RTC详细状态信息
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_show_status(void);

/**
 * @brief 运行RTC演示程序（显示当前时间、状态等）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_demo(void);

/**
 * @brief 启用LVGL时间显示更新
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_enable_lvgl_update(void);

/**
 * @brief 禁用LVGL时间显示更新
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_disable_lvgl_update(void);

/**
 * @brief 手动更新LVGL时间显示
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_update_lvgl_display(void);

/**
 * @brief 轮询检查并更新LVGL时间显示（主函数循环调用）
 * 这个函数应该在主循环中定期调用，它会自动检测分钟变化并更新界面
 */
void hardware_rtc_poll_update_lvgl(void);

/**
 * @brief 反初始化硬件RTC（清理资源）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t hardware_rtc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_RTC_H */
