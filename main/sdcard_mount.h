#ifndef SDCARD_MOUNT_H
#define SDCARD_MOUNT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdcard.h"  // 重用现有的结构定义

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 简化的SD卡初始化和挂载函数
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_mount_simple(void);

/**
 * @brief 简化的SD卡卸载函数
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_unmount_simple(void);

/**
 * @brief 检查SD卡是否已挂载
 * @return true 已挂载，false 未挂载
 */
bool sdcard_is_mounted_simple(void);

/**
 * @brief 获取SD卡状态
 * @return SD卡状态枚举值
 */
sd_status_t sdcard_get_status_simple(void);

/**
 * @brief 测试SD卡读写功能
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_test_simple(void);

#ifdef __cplusplus
}
#endif

#endif /* SDCARD_MOUNT_H */
