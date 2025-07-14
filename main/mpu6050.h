/**
 * @file mpu6050.h
 * @brief MPU6050 Driver for ESP32-S3 DIY Watch Project
 * 
 * This driver provides I2C communication, data reading, formatting,
 * and FreeRTOS task management for the MPU6050 6-axis sensor.
 * 
 * Features:
 * - I2C communication with timeout protection
 * - Raw and converted data reading
 * - Attitude angle calculation (Roll, Pitch)
 * - Thread-safe data access
 * - Automatic error recovery
 * - FreeRTOS task for periodic reading
 * 
 * @author GitHub Copilot
 * @date 2025-01-21
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MPU6050 raw data structure
 * Contains 16-bit signed integer values directly from registers
 */
typedef struct {
    int16_t accel_x;    ///< Raw accelerometer X-axis value
    int16_t accel_y;    ///< Raw accelerometer Y-axis value
    int16_t accel_z;    ///< Raw accelerometer Z-axis value
    int16_t temp;       ///< Raw temperature value
    int16_t gyro_x;     ///< Raw gyroscope X-axis value
    int16_t gyro_y;     ///< Raw gyroscope Y-axis value
    int16_t gyro_z;     ///< Raw gyroscope Z-axis value
} mpu6050_raw_data_t;

/**
 * @brief MPU6050 converted data structure
 * Contains data converted to physical units
 */
typedef struct {
    float accel_x;      ///< Acceleration X-axis (g)
    float accel_y;      ///< Acceleration Y-axis (g)
    float accel_z;      ///< Acceleration Z-axis (g)
    float gyro_x;       ///< Angular velocity X-axis (degrees/second)
    float gyro_y;       ///< Angular velocity Y-axis (degrees/second)
    float gyro_z;       ///< Angular velocity Z-axis (degrees/second)
    float temperature;  ///< Temperature (Celsius)
    float roll;         ///< Roll angle (degrees)
    float pitch;        ///< Pitch angle (degrees)
    int64_t timestamp;  ///< Timestamp (microseconds)
} mpu6050_data_t;

/**
 * @brief Callback function type for MPU6050 data updates
 * 
 * @param data Pointer to latest MPU6050 data
 * @param user_data User-defined data pointer
 */
typedef void (*mpu6050_data_callback_t)(const mpu6050_data_t* data, void* user_data);

/**
 * @brief Initialize I2C bus for MPU6050
 * 
 * Configures I2C master mode with the following settings:
 * - SDA: GPIO21, SCL: GPIO22
 * - Frequency: 400kHz
 * - Pull-up resistors enabled
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_i2c_init(void);

/**
 * @brief Initialize MPU6050 sensor
 * 
 * Performs the following initialization steps:
 * - Initialize I2C bus (if not already done)
 * - Check WHO_AM_I register
 * - Wake up sensor from sleep mode
 * - Configure accelerometer (±2g)
 * - Configure gyroscope (±250°/s)
 * - Set digital low-pass filter (256Hz)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_init(void);

/**
 * @brief Read raw sensor data from MPU6050
 * 
 * Reads 14 bytes of raw data from accelerometer, temperature,
 * and gyroscope registers in a single I2C transaction.
 * 
 * @param raw_data Pointer to store raw data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_raw(mpu6050_raw_data_t *raw_data);

/**
 * @brief Read and convert sensor data from MPU6050
 * 
 * Reads raw data and converts to physical units:
 * - Accelerometer: g (gravitational acceleration)
 * - Gyroscope: degrees per second
 * - Temperature: degrees Celsius
 * - Calculates roll and pitch angles
 * - Adds timestamp
 * 
 * @param data Pointer to store converted data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_read_data(mpu6050_data_t *data);

/**
 * @brief Print compact formatted sensor data (single line)
 * 
 * Prints all sensor data in a single compact line format,
 * suitable for continuous monitoring.
 * 
 * @param data Pointer to data structure to print
 */
void mpu6050_print_data_compact(const mpu6050_data_t *data);

/**
 * @brief Start MPU6050 reading task
 * 
 * Creates a FreeRTOS task that reads sensor data periodically:
 * - Reading frequency: 10Hz (100ms interval)
 * - Task priority: 5
 * - Stack size: 4096 bytes
 * - Updates latest data for thread-safe access
 * - Provides error recovery and statistics
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_start_reading_task(void);

/**
 * @brief Start MPU6050 reading task with custom interval
 * 
 * Creates a FreeRTOS task that reads sensor data periodically:
 * - Reading frequency: configurable
 * - Task priority: 5
 * - Stack size: 4096 bytes
 * - Updates latest data for thread-safe access
 * - Provides error recovery and statistics
 * 
 * @param interval_ms Reading interval in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_start_reading_task_with_interval(uint32_t interval_ms);

/**
 * @brief Stop MPU6050 reading task
 * 
 * Stops and deletes the background reading task.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_stop_reading_task(void);

/**
 * @brief Check if MPU6050 is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool mpu6050_is_initialized(void);

/**
 * @brief Check if MPU6050 reading task is running
 * 
 * @return true if task is running, false otherwise
 */
bool mpu6050_is_task_running(void);

/**
 * @brief Set data update callback
 * 
 * Sets a callback function that will be called whenever new
 * MPU6050 data is available from the reading task.
 * 
 * @param callback Callback function to call with new data
 * @param user_data User data to pass to callback
 */
void mpu6050_set_data_callback(mpu6050_data_callback_t callback, void* user_data);

/**
 * @brief Deinitialize MPU6050 and cleanup resources
 * 
 * Performs cleanup:
 * - Stops reading task
 * - Puts sensor to sleep mode
 * - Deletes mutex
 * - Uninstalls I2C driver
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mpu6050_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* MPU6050_H */