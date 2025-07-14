#ifndef SCREEN_POWER_H
#define SCREEN_POWER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 屏幕电源管理初始化
 * @return ESP_OK on success
 */
esp_err_t screen_power_init(void);

/**
 * @brief 通知触摸事件，重置息屏计时器
 */
void screen_power_touch_activity(void);

/**
 * @brief 检查是否应该息屏，并执行相应操作
 * 需要在主循环中定期调用
 */
void screen_power_check_sleep(void);

/**
 * @brief 强制唤醒屏幕
 */
void screen_power_wake_up(void);

/**
 * @brief 强制息屏
 */
void screen_power_sleep(void);

/**
 * @brief 获取当前屏幕状态
 * @return true - 屏幕已唤醒，false - 屏幕已息屏
 */
bool screen_power_is_awake(void);

/**
 * @brief 设置息屏超时时间（秒）
 * @param timeout_seconds 超时时间，默认15秒
 */
void screen_power_set_timeout(uint32_t timeout_seconds);

/**
 * @brief 设置LCD面板句柄（内部使用）
 * @param panel LCD面板句柄
 */
void screen_power_set_panel_handle(void* panel);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_POWER_H
