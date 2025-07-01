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
 * Initialize I2C bus for MPU6050
 */
esp_err_t mpu6050_i2c_init(void)
{
    if (mpu6050_initialized) {
        ESP_LOGW(TAG, "I2C already initialized");
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MPU6050_I2C_SDA_PIN,
        .scl_io_num = MPU6050_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = MPU6050_I2C_FREQ,
    };

    esp_err_t ret = i2c_param_config(MPU6050_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(MPU6050_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C initialized successfully (SDA: GPIO%d, SCL: GPIO%d, Freq: %d Hz)",
             MPU6050_I2C_SDA_PIN, MPU6050_I2C_SCL_PIN, MPU6050_I2C_FREQ);

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

    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I value: 0x%02X (expected 0x68)", who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "MPU6050 detected, WHO_AM_I: 0x%02X", who_am_i);

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
 * Get the latest sensor data (thread-safe)
 */
esp_err_t mpu6050_get_latest_data(mpu6050_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid data pointer");
        return ESP_ERR_INVALID_ARG;
    }

    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "Data mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE) {
        *data = latest_data;
        xSemaphoreGive(data_mutex);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to take data mutex");
    return ESP_ERR_TIMEOUT;
}

/**
 * Print formatted sensor data
 */
void mpu6050_print_data(const mpu6050_data_t *data)
{
    if (data == NULL) {
        ESP_LOGE(TAG, "Invalid data pointer for printing");
        return;
    }

    ESP_LOGI(TAG, "MPU6050 Data:");
    ESP_LOGI(TAG, "  Accelerometer: X=%.3f g, Y=%.3f g, Z=%.3f g",
             data->accel_x, data->accel_y, data->accel_z);
    ESP_LOGI(TAG, "  Gyroscope:     X=%.2f°/s, Y=%.2f°/s, Z=%.2f°/s",
             data->gyro_x, data->gyro_y, data->gyro_z);
    ESP_LOGI(TAG, "  Temperature:   %.2f°C", data->temperature);
    ESP_LOGI(TAG, "  Orientation:   Roll=%.1f°, Pitch=%.1f°",
             data->roll, data->pitch);
    ESP_LOGI(TAG, "  Timestamp:     %lld μs", data->timestamp);
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

    // Uninstall I2C driver
    esp_err_t ret = i2c_driver_delete(MPU6050_I2C_PORT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to delete I2C driver: %s", esp_err_to_name(ret));
    }

    mpu6050_initialized = false;
    ESP_LOGI(TAG, "MPU6050 deinitialized");

    return ESP_OK;
}