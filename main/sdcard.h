#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"

#ifdef __cplusplus
extern "C" {
#endif

// SD卡挂载点
#define SD_MOUNT_POINT "/sdcard"

// SD卡状态枚举
typedef enum {
    SD_STATUS_NOT_INITIALIZED = 0,
    SD_STATUS_INITIALIZING,
    SD_STATUS_MOUNTED,
    SD_STATUS_UNMOUNTED,
    SD_STATUS_ERROR
} sd_status_t;

// SD卡信息结构体
typedef struct {
    uint64_t total_bytes;       // 总容量（字节）
    uint64_t used_bytes;        // 已使用（字节）
    uint32_t sector_size;       // 扇区大小
    uint32_t sector_count;      // 扇区数量
    char card_name[64];         // 卡名称
    bool is_mounted;            // 是否已挂载
    sd_status_t status;         // 当前状态
} sd_card_info_t;

/**
 * @brief 初始化并挂载SD卡
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_init(void);

/**
 * @brief 卸载SD卡
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_deinit(void);

/**
 * @brief 检查SD卡是否已挂载
 * @return true 已挂载，false 未挂载
 */
bool sdcard_is_mounted(void);

/**
 * @brief 获取SD卡状态
 * @return SD卡状态枚举值
 */
sd_status_t sdcard_get_status(void);

/**
 * @brief 获取SD卡信息
 * @param info 输出SD卡信息的结构体指针
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_get_info(sd_card_info_t *info);

/**
 * @brief 格式化SD卡（慎用！会删除所有数据）
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_format(void);

/**
 * @brief 测试SD卡读写功能
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_test_rw(void);

/**
 * @brief 创建测试文件
 * @param filename 文件名
 * @param content 文件内容
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_create_test_file(const char *filename, const char *content);

/**
 * @brief 读取文件内容
 * @param filename 文件名
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_read_file(const char *filename, char *buffer, size_t buffer_size);

/**
 * @brief 列出SD卡根目录内容
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_list_files(void);

/**
 * @brief 获取可读的容量字符串
 * @param bytes 字节数
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 */
void sdcard_format_size(uint64_t bytes, char *buffer, size_t buffer_size);

/**
 * @brief 解释SDIO总线速度决定因素
 */
void sdcard_explain_speed_factors(void);

/**
 * @brief 测试不同SDIO速度设置
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t sdcard_test_different_speeds(void);

#ifdef __cplusplus
}
#endif

#endif /* SDCARD_H */
