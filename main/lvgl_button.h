#ifndef LVGL_BUTTON_H
#define LVGL_BUTTON_H

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// 按钮事件类型枚举
typedef enum {
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS,
    BUTTON_EVENT_DOUBLE_CLICK
} button_event_type_t;

// 按钮事件结构体
typedef struct {
    button_event_type_t type;
    uint32_t timestamp;
} button_event_t;

// 按钮状态枚举
typedef enum {
    BTN_STATE_IDLE,
    BTN_STATE_PRESSED,
    BTN_STATE_HELD,
    BTN_STATE_RELEASED
} button_state_t;

// 按钮事件回调函数类型
typedef void (*button_event_cb_t)(void);

// 按钮统计信息结构体
typedef struct {
    button_state_t current_state;
    bool is_waiting_double_click;
    uint32_t last_press_duration;
    TaskHandle_t task_handle;
} button_stats_t;

/**
 * @brief 初始化BOOT按钮
 * 配置GPIO并创建按钮检测任务
 * 设置默认的按钮事件处理函数
 */
void init_boot_btn(void);

/**
 * @brief 获取按钮当前状态
 * @return 按钮状态枚举值
 */
button_state_t get_button_state(void);

/**
 * @brief 获取按钮状态字符串（用于调试）
 * @return 按钮状态的字符串表示
 */
const char* get_button_state_string(void);

/**
 * @brief 获取按钮统计信息
 * @param stats 指向统计信息结构体的指针
 */
void get_button_statistics(button_stats_t *stats);

/**
 * @brief 重置按钮状态到IDLE（用于调试或故障恢复）
 */
void reset_button_state(void);

/**
 * @brief 检查按钮任务是否正在运行
 * @return true 如果任务正在运行，false 否则
 */
bool is_button_task_running(void);

/**
 * @brief 获取按钮事件队列句柄
 * @return 事件队列句柄，如果未初始化则返回NULL
 */
QueueHandle_t get_button_event_queue(void);

/**
 * @brief 处理按钮事件（在主任务中调用）
 * @param event 按钮事件
 */
void handle_button_event(const button_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_BUTTON_H */
