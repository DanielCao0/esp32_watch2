#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 检查WiFi是否已连接
 * @return true - WiFi已连接，false - WiFi未连接
 */
bool is_wifi_connected(void);

/**
 * @brief 初始化时钟系统
 * 设置时区为北京时间，创建15分钟定时器自动同步时间
 * 主函数应该调用此函数来初始化时钟系统
 */
void clock_init(void);

/**
 * @brief 停止时钟系统
 * 停止并删除定时器，清理资源
 */
void clock_deinit(void);

/**
 * @brief 获取北京时间（东八区）
 * 通过NTP服务器同步时间并设置为北京时间
 * 需要WiFi连接才能正常工作
 */
void obtain_beijing_time(void);

/**
 * @brief 应用程序获取北京时间的示例函数
 * 会先检查WiFi连接状态，然后获取时间
 */
void app_get_beijing_time(void);

/**
 * @brief 获取当前的本地时间信息
 * @param timeinfo 指向tm结构的指针，用于存储时间信息
 * @return true - 成功获取时间，false - 时间无效
 */
bool get_current_time(struct tm *timeinfo);

/**
 * @brief 格式化时间字符串
 * @param timeinfo 时间信息结构
 * @param time_str 输出的时间字符串缓冲区
 * @param max_len 缓冲区最大长度
 * @param format 时间格式字符串（如："%H:%M:%S"）
 */
void format_time_string(const struct tm *timeinfo, char *time_str, size_t max_len, const char *format);

/**
 * @brief 获取格式化的当前时间字符串
 * @param time_str 输出的时间字符串缓冲区
 * @param max_len 缓冲区最大长度
 * @param format 时间格式字符串
 * @return true - 成功获取，false - 失败
 */
bool get_formatted_time(char *time_str, size_t max_len, const char *format);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_H */
