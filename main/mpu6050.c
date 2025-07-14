/**
 * MPU6050 Driver for ESP32-S3 DIY Watch Project
 * Features: I2C communication, data reading, formatting, FreeRTOS task
 * Author: GitHub Copilot
 * Date: 2025-01-21
 */

#include "mpu6050.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "MPU6050";

// MPU6050 I2C configuration
#define MPU6050_I2C_PORT        I2C_NUM_0
#define MPU6050_I2C_SDA_PIN     46
#define MPU6050_I2C_SCL_PIN     3
#define MPU6050_I2C_FREQ        100000  // 100kHz
#define MPU6050_I2C_TIMEOUT     1000    // 1 second timeout

// MPU6050 I2C address
#define MPU6050_I2C_ADDR        0x68

// MPU6050 Register addresses
#define MPU6050_REG_PWR_MGMT_1  0x6B
#define MPU6050_REG_CONFIG      0x1A
#define MPU6050_REG_GYRO_CONFIG 0x1B
#define MPU6050_REG_ACCEL_CONFIG 0x1C
#define MPU6050_REG_ACCEL_XOUT_H 0x3B
#define MPU6050_REG_GYRO_XOUT_H  0x43
#define MPU6050_REG_TEMP_OUT_H   0x41
#define MPU6050_REG_WHO_AM_I     0x75

// Configuration values
#define MPU6050_DLPF_BW_256     0x00
#define MPU6050_GYRO_FS_250     0x00
#define MPU6050_ACCEL_FS_2G     0x00
#define MPU6050_CLOCK_PLL_XGYRO 0x01

// Scale factors for data conversion
#define MPU6050_ACCEL_SCALE_2G  16384.0f
#define MPU6050_GYRO_SCALE_250  131.0f
#define MPU6050_TEMP_SCALE      340.0f
#define MPU6050_TEMP_OFFSET     36.53f

// Global variables
static bool mpu6050_initialized = false;
static TaskHandle_t mpu6050_task_handle = NULL;
static mpu6050_data_t latest_data = {0};
static SemaphoreHandle_t data_mutex = NULL;
static uint32_t reading_interval_ms = 100;  // Default 100ms interval

// Callback support
static mpu6050_data_callback_t data_callback = NULL;
static void* callback_user_data = NULL;

/**
 * Write a single byte to MPU6050 register
 */
static esp_err_t mpu6050_write_byte(uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    
    return i2c_master_write_to_device(MPU6050_I2C_PORT, MPU6050_I2C_ADDR,
                                      write_buf, sizeof(write_buf),
                                      MPU6050_I2C_TIMEOUT / portTICK_PERIOD_MS);
}

/**
 * Read multiple bytes from MPU6050 registers
 */
static esp_err_t mpu6050_read_bytes(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(MPU6050_I2C_PORT, MPU6050_I2C_ADDR,
                                        &reg_addr, 1, data, len,
                                        MPU6050_I2C_TIMEOUT / portTICK_PERIOD_MS);
}

/**
 * Read a single byte from MPU6050 register
 */
static esp_err_t mpu6050_read_byte(uint8_t reg_addr, uint8_t *data)
{
    return mpu6050_read_bytes(reg_addr, data, 1);
}

/**
 * Convert two bytes to signed 16-bit integer
 */
static int16_t mpu6050_bytes_to_int16(uint8_t high, uint8_t low)
{
    return (int16_t)((high << 8) | low);
}

/**
 * Calculate roll angle from accelerometer data (in degrees)
 */
static float mpu6050_calculate_roll(float accel_x, float accel_y, float accel_z)
{
    return atan2(accel_y, sqrt(accel_x * accel_x + accel_z * accel_z)) * 180.0 / M_PI;
}

/**
 * Calculate pitch angle from accelerometer data (in degrees)
 */
static float mpu6050_calculate_pitch(float accel_x, float accel_y, float accel_z)
{
    return atan2(-accel_x, sqrt(accel_y * accel_y + accel_z * accel_z)) * 180.0 / M_PI;
}

/**
 * Initialize I2C bus for MPU6050 (Skip if already initialized by touch screen)
 */
esp_err_t mpu6050_i2c_init(void)
{
    // I2C总线已经被触摸屏初始化过了，直接返回成功
    // Touch screen has already initialized the I2C bus, just return success
    ESP_LOGI(TAG, "Using existing I2C bus (SDA: GPIO%d, SCL: GPIO%d) - already initialized by touch screen",
             MPU6050_I2C_SDA_PIN, MPU6050_I2C_SCL_PIN);
    
    return ESP_OK;
}



/**
 * Initialize MPU6050 sensor
 */
esp_err_t mpu6050_init(void)
{
    esp_err_t ret;

    // Initialize I2C if not already done
    ret = mpu6050_i2c_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Create data mutex
    if (data_mutex == NULL) {
        data_mutex = xSemaphoreCreateMutex();
        if (data_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create data mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Check WHO_AM_I register
    uint8_t who_am_i;
    ret = mpu6050_read_byte(MPU6050_REG_WHO_AM_I, &who_am_i);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Read WHO_AM_I register: 0x%02X", who_am_i);

    // Check for different possible sensor types
    if (who_am_i == 0x68) {
        ESP_LOGI(TAG, "MPU6050 detected (WHO_AM_I: 0x68)");
    } else if (who_am_i == 0x70) {
        ESP_LOGI(TAG, "MPU6500 detected (WHO_AM_I: 0x70) - compatible with MPU6050");
    } else if (who_am_i == 0x98) {
        ESP_LOGI(TAG, "ICM20602 detected (WHO_AM_I: 0x98) - trying compatibility mode");
        // ICM20602 is generally compatible with MPU6050 register layout
    } else if (who_am_i == 0x11) {
        ESP_LOGI(TAG, "ICM20648 detected (WHO_AM_I: 0x11) - trying compatibility mode");
    } else {
        ESP_LOGW(TAG, "Unknown sensor detected (WHO_AM_I: 0x%02X)", who_am_i);
        ESP_LOGW(TAG, "Attempting to continue with MPU6050 compatibility mode...");
        ESP_LOGW(TAG, "Note: This sensor may not be fully compatible with MPU6050");
    }

    // Wake up the MPU6050 (exit sleep mode)
    ret = mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, MPU6050_CLOCK_PLL_XGYRO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set sample rate divider (default DLPF)
    ret = mpu6050_write_byte(MPU6050_REG_CONFIG, MPU6050_DLPF_BW_256);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sample rate: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure gyroscope (±250°/s)
    ret = mpu6050_write_byte(MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_250);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure gyroscope: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure accelerometer (±2g)
    ret = mpu6050_write_byte(MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_2G);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure accelerometer: %s", esp_err_to_name(ret));
        return ret;
    }

    mpu6050_initialized = true;
    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    ESP_LOGI(TAG, "Configuration: Accel ±2g, Gyro ±250°/s, DLPF 256Hz");

    return ESP_OK;
}

/**
 * Read raw sensor data from MPU6050
 */
esp_err_t mpu6050_read_raw(mpu6050_raw_data_t *raw_data)
{
    if (!mpu6050_initialized) {
        ESP_LOGE(TAG, "MPU6050 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (raw_data == NULL) {
        ESP_LOGE(TAG, "Invalid raw_data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[14];
    esp_err_t ret = mpu6050_read_bytes(MPU6050_REG_ACCEL_XOUT_H, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sensor data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert raw bytes to signed integers
    raw_data->accel_x = mpu6050_bytes_to_int16(data[0], data[1]);
    raw_data->accel_y = mpu6050_bytes_to_int16(data[2], data[3]);
    raw_data->accel_z = mpu6050_bytes_to_int16(data[4], data[5]);
    raw_data->temp = mpu6050_bytes_to_int16(data[6], data[7]);
    raw_data->gyro_x = mpu6050_bytes_to_int16(data[8], data[9]);
    raw_data->gyro_y = mpu6050_bytes_to_int16(data[10], data[11]);
    raw_data->gyro_z = mpu6050_bytes_to_int16(data[12], data[13]);

    return ESP_OK;
}

/**
 * Read and convert sensor data from MPU6050
 */
esp_err_t mpu6050_read_data(mpu6050_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    mpu6050_raw_data_t raw_data;
    esp_err_t ret = mpu6050_read_raw(&raw_data);
    if (ret != ESP_OK) {
        return ret;
    }

    // Convert raw data to physical units
    data->accel_x = raw_data.accel_x / MPU6050_ACCEL_SCALE_2G;
    data->accel_y = raw_data.accel_y / MPU6050_ACCEL_SCALE_2G;
    data->accel_z = raw_data.accel_z / MPU6050_ACCEL_SCALE_2G;

    data->gyro_x = raw_data.gyro_x / MPU6050_GYRO_SCALE_250;
    data->gyro_y = raw_data.gyro_y / MPU6050_GYRO_SCALE_250;
    data->gyro_z = raw_data.gyro_z / MPU6050_GYRO_SCALE_250;

    data->temperature = raw_data.temp / MPU6050_TEMP_SCALE + MPU6050_TEMP_OFFSET;

    // Calculate roll and pitch angles
    data->roll = mpu6050_calculate_roll(data->accel_x, data->accel_y, data->accel_z);
    data->pitch = mpu6050_calculate_pitch(data->accel_x, data->accel_y, data->accel_z);

    // Get timestamp
    data->timestamp = esp_timer_get_time();

    return ESP_OK;
}

/**
 * Print compact formatted sensor data (single line)
 */
void mpu6050_print_data_compact(const mpu6050_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid data pointer for compact printing");
        return;
    }

    ESP_LOGI(TAG, "MPU6050: Accel[%.2f,%.2f,%.2f]g Gyro[%.1f,%.1f,%.1f]°/s Temp=%.1f°C Roll=%.1f° Pitch=%.1f°",
             data->accel_x, data->accel_y, data->accel_z,
             data->gyro_x, data->gyro_y, data->gyro_z,
             data->temperature, data->roll, data->pitch);
}

/**
 * MPU6050 reading task (runs periodically)
 */
static void mpu6050_reading_task(void *pvParameters)
{
    const TickType_t delay = pdMS_TO_TICKS(reading_interval_ms);
    mpu6050_data_t data;
    uint32_t error_count = 0;
    uint32_t success_count = 0;
    uint32_t print_interval = (reading_interval_ms >= 1000) ? 5 : (5000 / reading_interval_ms);  // Print every 5 seconds

    ESP_LOGI(TAG, "MPU6050 reading task started (%lu ms interval, %.1f Hz)", reading_interval_ms, 1000.0 / reading_interval_ms);

    while (1) {
        esp_err_t ret = mpu6050_read_data(&data);
        if (ret == ESP_OK) {
            // Update latest data (thread-safe)
            if (data_mutex != NULL && xSemaphoreTake(data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                latest_data = data;
                xSemaphoreGive(data_mutex);
            }

            // Call the registered callback function if available
            if (data_callback != NULL) {
                data_callback(&data, callback_user_data);
            }

            success_count++;

            // Print data every print_interval readings (approximately every 5 seconds)
            if (success_count % print_interval == 0) {
                mpu6050_print_data_compact(&data);
                ESP_LOGI(TAG, "MPU6050 Statistics: Success=%lu, Errors=%lu", success_count, error_count);
            }
        } else {
            error_count++;
            ESP_LOGW(TAG, "Failed to read MPU6050 data: %s (Error count: %lu)", esp_err_to_name(ret), error_count);

            // If too many consecutive errors, try to reinitialize
            if (error_count % 10 == 0) {
                ESP_LOGW(TAG, "Too many errors, attempting to reinitialize MPU6050...");
                mpu6050_initialized = false;
                vTaskDelay(pdMS_TO_TICKS(100));
                if (mpu6050_init() == ESP_OK) {
                    ESP_LOGI(TAG, "MPU6050 reinitialized successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to reinitialize MPU6050");
                }
            }
        }

        vTaskDelay(delay);
    }
}

/**
 * Start MPU6050 reading task
 */
esp_err_t mpu6050_start_reading_task(void)
{
    return mpu6050_start_reading_task_with_interval(100);  // Default 100ms interval
}

/**
 * Start MPU6050 reading task with custom interval
 */
esp_err_t mpu6050_start_reading_task_with_interval(uint32_t interval_ms)
{
    if (mpu6050_task_handle != NULL) {
        ESP_LOGW(TAG, "MPU6050 reading task already running");
        return ESP_OK;
    }

    if (!mpu6050_initialized) {
        ESP_LOGE(TAG, "MPU6050 not initialized, call mpu6050_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    if (interval_ms < 10 || interval_ms > 60000) {
        ESP_LOGE(TAG, "Invalid interval: %lu ms (must be 10-60000)", interval_ms);
        return ESP_ERR_INVALID_ARG;
    }

    // Set the reading interval
    reading_interval_ms = interval_ms;

    BaseType_t ret = xTaskCreate(
        mpu6050_reading_task,
        "mpu6050_task",
        4096,                           // Stack size
        NULL,                           // Task parameter
        5,                              // Task priority (same as other sensors)
        &mpu6050_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MPU6050 reading task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "MPU6050 reading task started successfully with %lu ms interval", interval_ms);
    return ESP_OK;
}

/**
 * Stop MPU6050 reading task
 */
esp_err_t mpu6050_stop_reading_task(void)
{
    if (mpu6050_task_handle == NULL) {
        ESP_LOGW(TAG, "MPU6050 reading task not running");
        return ESP_OK;
    }

    vTaskDelete(mpu6050_task_handle);
    mpu6050_task_handle = NULL;

    ESP_LOGI(TAG, "MPU6050 reading task stopped");
    return ESP_OK;
}

/**
 * Set data update callback
 */
void mpu6050_set_data_callback(mpu6050_data_callback_t callback, void* user_data)
{
    data_callback = callback;
    callback_user_data = user_data;
    
    if (callback != NULL) {
        ESP_LOGI(TAG, "MPU6050 data callback registered");
    } else {
        ESP_LOGI(TAG, "MPU6050 data callback unregistered");
    }
}

/**
 * Check if MPU6050 is initialized
 */
bool mpu6050_is_initialized(void)
{
    return mpu6050_initialized;
}

/**
 * Check if MPU6050 reading task is running
 */
bool mpu6050_is_task_running(void)
{
    return (mpu6050_task_handle != NULL);
}

/**
 * Deinitialize MPU6050 and cleanup resources
 */
esp_err_t mpu6050_deinit(void)
{
    // Stop reading task
    mpu6050_stop_reading_task();

    // Put MPU6050 to sleep mode
    if (mpu6050_initialized) {
        mpu6050_write_byte(MPU6050_REG_PWR_MGMT_1, 0x40);  // Sleep mode
    }

    // Delete mutex
    if (data_mutex != NULL) {
        vSemaphoreDelete(data_mutex);
        data_mutex = NULL;
    }

    // 不要删除I2C驱动，因为触摸屏还在使用
    // Don't delete I2C driver as it's shared with touch screen
    ESP_LOGI(TAG, "MPU6050 deinitialized (I2C driver kept for touch screen)");

    mpu6050_initialized = false;

    return ESP_OK;
}

/**
 * Register a callback function for MPU6050 data
 */
esp_err_t mpu6050_register_data_callback(mpu6050_data_callback_t callback, void* user_data)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Invalid callback function");
        return ESP_ERR_INVALID_ARG;
    }

    data_callback = callback;
    callback_user_data = user_data;

    ESP_LOGI(TAG, "MPU6050 data callback registered");
    return ESP_OK;
}

/**
 * Unregister the callback function for MPU6050 data
 */
esp_err_t mpu6050_unregister_data_callback(void)
{
    data_callback = NULL;
    callback_user_data = NULL;

    ESP_LOGI(TAG, "MPU6050 data callback unregistered");
    return ESP_OK;
}