#ifndef MPU6050_H
#define MPU6050_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MPU6050转换后的数据结构
 */
typedef struct {
    float accel_x;      ///< 加速度X轴 (g)
    float accel_y;      ///< 加速度Y轴 (g)
    float accel_z;      ///< 加速度Z轴 (g)
    float temperature;  ///< 温度 (°C)
    float gyro_x;       ///< 陀螺仪X轴 (°/s)
    float gyro_y;       ///< 陀螺仪Y轴 (°/s)
    float gyro_z;       ///< 陀螺仪Z轴 (°/s)
} mpu6050_data_t;

/**
 * @brief 初始化MPU6050
 * 
 * @return
 *     - ESP_OK: 初始化成功
 *     - ESP_ERR_INVALID_RESPONSE: 设备ID验证失败
 *     - 其他错误码: I2C通信错误
 */
esp_err_t mpu6050_init(void);

/**
 * @brief 启动MPU6050数据读取任务
 * 
 * @return
 *     - ESP_OK: 任务启动成功
 *     - ESP_ERR_INVALID_STATE: MPU6050未初始化
 *     - ESP_ERR_NO_MEM: 内存不足，无法创建任务
 */
esp_err_t mpu6050_start_reading_task(void);

/**
 * @brief 停止MPU6050数据读取任务
 * 
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t mpu6050_stop_reading_task(void);

/**
 * @brief 反初始化MPU6050
 * 
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t mpu6050_deinit(void);

/**
 * @brief 检查MPU6050是否已初始化
 * 
 * @return
 *     - true: 已初始化
 *     - false: 未初始化
 */
bool mpu6050_is_initialized(void);

/**
 * @brief 获取当前MPU6050数据（单次读取）
 * 
 * @param data 存储读取数据的指针
 * @return
 *     - ESP_OK: 读取成功
 *     - ESP_ERR_INVALID_STATE: MPU6050未初始化
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - 其他错误码: I2C通信错误
 */
esp_err_t mpu6050_get_data(mpu6050_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */
