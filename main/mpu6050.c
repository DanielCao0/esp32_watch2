#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"
#include "mpu6050.h"

static const char *TAG = "MPU6050";

// I2C配置
#define I2C_MASTER_SCL_IO           3      /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO           46      /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0       /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          100000  /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0       /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

// MPU6050相关配置
#define MPU6050_SENSOR_ADDR         0x68    /*!< Slave address of the MPU6050 sensor */
#define MPU6050_WHO_AM_I_REG_ADDR   0x75    /*!< Register addresses of the "who am i" register */
#define MPU6050_PWR_MGMT_1_REG_ADDR 0x6B    /*!< Register addresses of the power managment register */
#define MPU6050_RESET_BIT           7

// 数据寄存器地址
#define MPU6050_ACCEL_XOUT_H        0x3B
#define MPU6050_ACCEL_XOUT_L        0x3C
#define MPU6050_ACCEL_YOUT_H        0x3D
#define MPU6050_ACCEL_YOUT_L        0x3E
#define MPU6050_ACCEL_ZOUT_H        0x3F
#define MPU6050_ACCEL_ZOUT_L        0x40
#define MPU6050_TEMP_OUT_H          0x41
#define MPU6050_TEMP_OUT_L          0x42
#define MPU6050_GYRO_XOUT_H         0x43
#define MPU6050_GYRO_XOUT_L         0x44
#define MPU6050_GYRO_YOUT_H         0x45
#define MPU6050_GYRO_YOUT_L         0x46
#define MPU6050_GYRO_ZOUT_H         0x47
#define MPU6050_GYRO_ZOUT_L         0x48

// 任务配置
#define MPU6050_READ_INTERVAL_MS    100     /*!< 数据读取间隔：100ms */
#define MPU6050_TASK_STACK_SIZE     4096    /*!< 任务栈大小 */
#define MPU6050_TASK_PRIORITY       5       /*!< 任务优先级 */

// 全局变量
static bool mpu6050_initialized = false;
static TaskHandle_t mpu6050_task_handle = NULL;

// MPU6050数据结构
typedef struct {
    int16_t accel_x;    // 加速度X轴
    int16_t accel_y;    // 加速度Y轴
    int16_t accel_z;    // 加速度Z轴
    int16_t temp;       // 温度
    int16_t gyro_x;     // 陀螺仪X轴
    int16_t gyro_y;     // 陀螺仪Y轴
    int16_t gyro_z;     // 陀螺仪Z轴
} mpu6050_raw_data_t;

/**
 * @brief 读取MPU6050寄存器
 */
static esp_err_t mpu6050_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief 写入MPU6050寄存器
 */
static esp_err_t mpu6050_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_SENSOR_ADDR, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return ret;
}

/**
 * @brief 初始化I2C总线
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

/**
 * @brief 检查MPU6050设备ID
 */
static esp_err_t mpu6050_check_device_id(void)
{
    uint8_t data;
    esp_err_t ret = mpu6050_register_read(MPU6050_WHO_AM_I_REG_ADDR, &data, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        return ret;
    }

    ESP_LOGI(TAG, "WHO_AM_I register value: 0x%02X", data);
    
    // MPU6050的WHO_AM_I寄存器应该返回0x68
    if (data != 0x68) {
        ESP_LOGE(TAG, "Invalid device ID. Expected 0x68, got 0x%02X", data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "MPU6050 device ID verified successfully");
    return ESP_OK;
}

/**
 * @brief 初始化MPU6050
 */
static esp_err_t mpu6050_init_device(void)
{
    esp_err_t ret;

    // 复位设备
    ret = mpu6050_register_write_byte(MPU6050_PWR_MGMT_1_REG_ADDR, 1 << MPU6050_RESET_BIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset MPU6050");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 等待复位完成

    // 退出睡眠模式
    ret = mpu6050_register_write_byte(MPU6050_PWR_MGMT_1_REG_ADDR, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    return ESP_OK;
}

/**
 * @brief 读取MPU6050原始数据
 */
static esp_err_t mpu6050_read_raw_data(mpu6050_raw_data_t *raw_data)
{
    uint8_t data[14];
    esp_err_t ret = mpu6050_register_read(MPU6050_ACCEL_XOUT_H, data, sizeof(data));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sensor data");
        return ret;
    }

    // 组合高低字节数据
    raw_data->accel_x = (int16_t)((data[0] << 8) | data[1]);
    raw_data->accel_y = (int16_t)((data[2] << 8) | data[3]);
    raw_data->accel_z = (int16_t)((data[4] << 8) | data[5]);
    raw_data->temp    = (int16_t)((data[6] << 8) | data[7]);
    raw_data->gyro_x  = (int16_t)((data[8] << 8) | data[9]);
    raw_data->gyro_y  = (int16_t)((data[10] << 8) | data[11]);
    raw_data->gyro_z  = (int16_t)((data[12] << 8) | data[13]);

    return ESP_OK;
}

/**
 * @brief 将原始数据转换为物理单位
 */
static void mpu6050_convert_data(const mpu6050_raw_data_t *raw_data, mpu6050_data_t *converted_data)
{
    // 加速度计：±2g量程，灵敏度16384 LSB/g
    converted_data->accel_x = (float)raw_data->accel_x / 16384.0;
    converted_data->accel_y = (float)raw_data->accel_y / 16384.0;
    converted_data->accel_z = (float)raw_data->accel_z / 16384.0;

    // 陀螺仪：±250°/s量程，灵敏度131 LSB/(°/s)
    converted_data->gyro_x = (float)raw_data->gyro_x / 131.0;
    converted_data->gyro_y = (float)raw_data->gyro_y / 131.0;
    converted_data->gyro_z = (float)raw_data->gyro_z / 131.0;

    // 温度：公式 Temperature = (TEMP_OUT)/340 + 36.53
    converted_data->temperature = (float)raw_data->temp / 340.0 + 36.53;
}

/**
 * @brief 打印MPU6050数据
 */
static void mpu6050_print_data(const mpu6050_data_t *data)
{
    ESP_LOGI(TAG, "┌─────────────────────── MPU6050 Data ───────────────────────┐");
    ESP_LOGI(TAG, "│ Accelerometer (g):  X=%+7.3f  Y=%+7.3f  Z=%+7.3f │", 
             data->accel_x, data->accel_y, data->accel_z);
    ESP_LOGI(TAG, "│ Gyroscope (°/s):    X=%+7.2f  Y=%+7.2f  Z=%+7.2f │", 
             data->gyro_x, data->gyro_y, data->gyro_z);
    ESP_LOGI(TAG, "│ Temperature (°C):   %+7.2f                          │", 
             data->temperature);
    ESP_LOGI(TAG, "└─────────────────────────────────────────────────────────────┘");
}

/**
 * @brief MPU6050数据读取任务
 */
static void mpu6050_read_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MPU6050 data reading task started");
    
    mpu6050_raw_data_t raw_data;
    mpu6050_data_t converted_data;
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 读取原始数据
        esp_err_t ret = mpu6050_read_raw_data(&raw_data);
        if (ret == ESP_OK) {
            // 转换为物理单位
            mpu6050_convert_data(&raw_data, &converted_data);
            
            // 打印数据
            mpu6050_print_data(&converted_data);
        } else {
            ESP_LOGE(TAG, "Failed to read MPU6050 data: %s", esp_err_to_name(ret));
        }
        
        // 等待下次读取
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(MPU6050_READ_INTERVAL_MS));
    }
}

/**
 * @brief 初始化MPU6050系统
 */
esp_err_t mpu6050_init(void)
{
    esp_err_t ret;
    
    if (mpu6050_initialized) {
        ESP_LOGW(TAG, "MPU6050 already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing MPU6050...");
    
    // 初始化I2C
    ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C master initialized successfully");
    
    // 检查设备ID
    ret = mpu6050_check_device_id();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化MPU6050设备
    ret = mpu6050_init_device();
    if (ret != ESP_OK) {
        return ret;
    }
    
    mpu6050_initialized = true;
    ESP_LOGI(TAG, "MPU6050 initialization completed successfully");
    
    return ESP_OK;
}

/**
 * @brief 启动MPU6050数据读取任务
 */
esp_err_t mpu6050_start_reading_task(void)
{
    if (!mpu6050_initialized) {
        ESP_LOGE(TAG, "MPU6050 not initialized. Call mpu6050_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mpu6050_task_handle != NULL) {
        ESP_LOGW(TAG, "MPU6050 reading task already running");
        return ESP_OK;
    }
    
    BaseType_t ret = xTaskCreate(
        mpu6050_read_task,
        "mpu6050_read",
        MPU6050_TASK_STACK_SIZE,
        NULL,
        MPU6050_TASK_PRIORITY,
        &mpu6050_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MPU6050 reading task");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "MPU6050 reading task started successfully");
    return ESP_OK;
}

/**
 * @brief 停止MPU6050数据读取任务
 */
esp_err_t mpu6050_stop_reading_task(void)
{
    if (mpu6050_task_handle != NULL) {
        vTaskDelete(mpu6050_task_handle);
        mpu6050_task_handle = NULL;
        ESP_LOGI(TAG, "MPU6050 reading task stopped");
    }
    
    return ESP_OK;
}

/**
 * @brief 反初始化MPU6050
 */
esp_err_t mpu6050_deinit(void)
{
    // 停止读取任务
    mpu6050_stop_reading_task();
    
    // 卸载I2C驱动
    i2c_driver_delete(I2C_MASTER_NUM);
    
    mpu6050_initialized = false;
    ESP_LOGI(TAG, "MPU6050 deinitialized");
    
    return ESP_OK;
}

/**
 * @brief 检查MPU6050是否已初始化
 */
bool mpu6050_is_initialized(void)
{
    return mpu6050_initialized;
}

/**
 * @brief 获取当前MPU6050数据（单次读取）
 */
esp_err_t mpu6050_get_data(mpu6050_data_t *data)
{
    if (!mpu6050_initialized) {
        ESP_LOGE(TAG, "MPU6050 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Data pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    mpu6050_raw_data_t raw_data;
    esp_err_t ret = mpu6050_read_raw_data(&raw_data);
    if (ret != ESP_OK) {
        return ret;
    }
    
    mpu6050_convert_data(&raw_data, data);
    return ESP_OK;
}