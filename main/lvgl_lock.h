#ifndef LVGL_LOCK_H
#define LVGL_LOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取LVGL互斥信号量
 * @param timeout_ms 超时时间（毫秒）
 * @return true - 成功获取，false - 超时失败
 * 
 * 在调用任何LVGL API之前，都应该先调用此函数获取互斥锁。
 * 这确保了LVGL的线程安全性。
 * 
 * 使用示例：
 * if (lvgl_lock(100)) {
 *     lv_label_set_text(label, "Hello");
 *     lvgl_unlock();
 * }
 */
bool lvgl_lock(uint32_t timeout_ms);

/**
 * @brief 释放LVGL互斥信号量
 * 
 * 在完成LVGL操作后，必须调用此函数释放互斥锁。
 * 每次成功调用lvgl_lock()后都必须调用对应的lvgl_unlock()。
 */
void lvgl_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_LOCK_H */
