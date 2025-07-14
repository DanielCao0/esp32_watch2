#include "sdcard.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

static const char *TAG = "sdcard_mount";

// SD卡相关配置 - ESP32-S3 SPI模式
#define PIN_NUM_MISO    13  // 根据您的硬件连接修改
#define PIN_NUM_MOSI    11  // 根据您的硬件连接修改
#define PIN_NUM_CLK     12  // 根据您的硬件连接修改
#define PIN_NUM_CS      10  // 根据您的硬件连接修改

// 全局变量
static sdmmc_card_t *card = NULL;
static sd_status_t current_status = SD_STATUS_NOT_INITIALIZED;
static bool is_initialized = false;

// 简化的SD卡初始化和挂载函数
esp_err_t sdcard_mount_simple(void)
{
    ESP_LOGI(TAG, "Initializing SD card (simplified version)");
    
    if (is_initialized) {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }
    
    current_status = SD_STATUS_INITIALIZING;
    
    esp_err_t ret;
    
    // 挂载配置
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // SPI总线配置
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // 初始化SPI总线
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        current_status = SD_STATUS_ERROR;
        return ret;
    }
    
    // SDSPI设备配置
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SPI2_HOST;
    
    ESP_LOGI(TAG, "Mounting filesystem...");
    
    // 挂载文件系统 - 使用ESP-IDF 5.4.1兼容的API
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, SPI2_HOST, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. SD card may need formatting.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check SD card connection.", esp_err_to_name(ret));
        }
        spi_bus_free(SPI2_HOST);
        current_status = SD_STATUS_ERROR;
        return ret;
    }
    
    current_status = SD_STATUS_MOUNTED;
    is_initialized = true;
    
    ESP_LOGI(TAG, "SD card mounted successfully at %s", SD_MOUNT_POINT);
    
    // 打印卡信息
    if (card != NULL) {
        sdmmc_card_print_info(stdout, card);
    }
    
    return ESP_OK;
}

// 简化的SD卡卸载函数
esp_err_t sdcard_unmount_simple(void)
{
    ESP_LOGI(TAG, "Unmounting SD card");
    
    if (!is_initialized) {
        ESP_LOGW(TAG, "SD card not initialized");
        return ESP_OK;
    }
    
    // 卸载文件系统
    esp_err_t ret = esp_vfs_fat_sdmmc_unmount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 释放SPI总线
    spi_bus_free(SPI2_HOST);
    
    card = NULL;
    is_initialized = false;
    current_status = SD_STATUS_UNMOUNTED;
    
    ESP_LOGI(TAG, "SD card unmounted successfully");
    return ESP_OK;
}

// 检查SD卡状态
bool sdcard_is_mounted_simple(void)
{
    return (is_initialized && current_status == SD_STATUS_MOUNTED);
}

// 获取SD卡状态
sd_status_t sdcard_get_status_simple(void)
{
    return current_status;
}

// 测试SD卡读写功能
esp_err_t sdcard_test_simple(void)
{
    ESP_LOGI(TAG, "Testing SD card read/write functionality");
    
    if (!sdcard_is_mounted_simple()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 创建测试文件
    char test_file_path[64];
    snprintf(test_file_path, sizeof(test_file_path), "%s/test_simple.txt", SD_MOUNT_POINT);
    
    // 写入测试
    FILE *f = fopen(test_file_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    
    const char *test_content = "SD Card Test - ESP32-S3 QSPI Watch\\n";
    fprintf(f, "%s", test_content);
    fprintf(f, "Timestamp: %llu ms\\n", esp_timer_get_time() / 1000);
    fclose(f);
    
    ESP_LOGI(TAG, "Test file written successfully");
    
    // 读取测试
    f = fopen(test_file_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    
    char read_buffer[256];
    size_t bytes_read = fread(read_buffer, 1, sizeof(read_buffer) - 1, f);
    read_buffer[bytes_read] = '\\0';
    fclose(f);
    
    ESP_LOGI(TAG, "Test file content:");
    ESP_LOGI(TAG, "%s", read_buffer);
    
    // 验证内容
    if (strstr(read_buffer, "SD Card Test") != NULL) {
        ESP_LOGI(TAG, "SD card test PASSED");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "SD card test FAILED - content mismatch");
        return ESP_FAIL;
    }
}
