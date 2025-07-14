#include "sdcard.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>

static const char *TAG = "sdcard";

// SD卡相关配置 - ESP32-S3 SDIO模式（4位数据线）
#define PIN_NUM_CLK     44   // RXD0 - SD_CLK 
#define PIN_NUM_CMD     42  // GPIO42 - SD_CMD
#define PIN_NUM_D0      43   // TXD0 - SD_D0
#define PIN_NUM_D1      2  // GPIO41 - SD_D1  
#define PIN_NUM_D2      40  // GPIO40 - SD_D2
#define PIN_NUM_D3      41  // GPIO39 - SD_D3
#define PIN_NUM_CD      -1  // Card Detect (如果有的话)

// 全局变量
static sdmmc_card_t *card = NULL;
static sd_status_t current_status = SD_STATUS_NOT_INITIALIZED;
static bool is_initialized = false;

// 获取可读的容量字符串
void sdcard_format_size(uint64_t bytes, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    
    if (bytes >= (1ULL << 30)) {
        snprintf(buffer, buffer_size, "%.2f GB", (double)bytes / (1ULL << 30));
    } else if (bytes >= (1ULL << 20)) {
        snprintf(buffer, buffer_size, "%.2f MB", (double)bytes / (1ULL << 20));
    } else if (bytes >= (1ULL << 10)) {
        snprintf(buffer, buffer_size, "%.2f KB", (double)bytes / (1ULL << 10));
    } else {
        snprintf(buffer, buffer_size, "%llu B", bytes);
    }
}

// 初始化并挂载SD卡 - SDIO模式
esp_err_t sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card via SDIO");
    
    if (is_initialized) {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }
    
    current_status = SD_STATUS_INITIALIZING;
    
    esp_err_t ret;
    
    // 配置挂载选项
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,    // 如果挂载失败，不要格式化
        .max_files = 5,                     // 最大同时打开文件数
        .allocation_unit_size = 16 * 1024   // 分配单元大小
    };
    
    ESP_LOGI(TAG, "Initializing SDMMC peripheral");
    
    // 配置SDMMC主机
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;  // 使用4位数据线
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    ESP_LOGI(TAG, "SDIO Configuration:");
    ESP_LOGI(TAG, "  Default max frequency: %d kHz (%d MHz)", host.max_freq_khz, host.max_freq_khz / 1000);
    ESP_LOGI(TAG, "  Available frequency options:");
    ESP_LOGI(TAG, "    SDMMC_FREQ_DEFAULT: %d kHz", SDMMC_FREQ_DEFAULT);
    ESP_LOGI(TAG, "    SDMMC_FREQ_HIGHSPEED: %d kHz", SDMMC_FREQ_HIGHSPEED);
    ESP_LOGI(TAG, "    SDMMC_FREQ_PROBING: %d kHz", SDMMC_FREQ_PROBING);
    
    // 您可以手动设置频率，例如：
    // host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 通常是40MHz
    // host.max_freq_khz = 25000;  // 25MHz 自定义频率
    
    // 配置SDMMC插槽
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = PIN_NUM_CLK;      // CLK
    slot_config.cmd = PIN_NUM_CMD;      // CMD
    slot_config.d0 = PIN_NUM_D0;        // D0
    slot_config.d1 = PIN_NUM_D1;        // D1
    slot_config.d2 = PIN_NUM_D2;        // D2
    slot_config.d3 = PIN_NUM_D3;        // D3
    slot_config.cd = PIN_NUM_CD;        // Card Detect (-1 if not used)
    slot_config.wp = GPIO_NUM_NC;       // Write Protect (not used)
    slot_config.width = 4;              // 4位数据线
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // 使用内部上拉
    
    ESP_LOGI(TAG, "Mounting filesystem");
    
    // 挂载文件系统 - SDIO模式
    ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. SD card may need formatting.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check SD card connection and power.", esp_err_to_name(ret));
        }
        current_status = SD_STATUS_ERROR;
        return ret;
    }
    
    current_status = SD_STATUS_MOUNTED;
    is_initialized = true;
    
    // 打印卡信息
    ESP_LOGI(TAG, "SD card mounted successfully via SDIO");
    sdmmc_card_print_info(stdout, card);
    
    // 添加详细的速度信息
    ESP_LOGI(TAG, "SD Card Speed Information:");
    ESP_LOGI(TAG, "  Card max frequency: %d kHz (%d MHz)", card->max_freq_khz, card->max_freq_khz / 1000);
    ESP_LOGI(TAG, "  Current frequency: %d kHz (%d MHz)", card->real_freq_khz, card->real_freq_khz / 1000);
    ESP_LOGI(TAG, "  Bus width: %d bits", (card->host.flags & SDMMC_HOST_FLAG_4BIT) ? 4 : 1);
    ESP_LOGI(TAG, "  Card capacity: %u sectors", card->csd.capacity);
    ESP_LOGI(TAG, "  Sector size: %u bytes", card->csd.sector_size);
    
    return ESP_OK;
}

// 卸载SD卡 - SDIO模式
esp_err_t sdcard_deinit(void)
{
    ESP_LOGI(TAG, "Unmounting SD card");
    
    if (!is_initialized) {
        ESP_LOGW(TAG, "SD card not initialized");
        return ESP_OK;
    }
    
    // 卸载文件系统 - SDIO模式
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SDIO模式不需要手动释放总线，由esp_vfs_fat_sdmmc_unmount处理
    
    card = NULL;
    is_initialized = false;
    current_status = SD_STATUS_UNMOUNTED;
    
    ESP_LOGI(TAG, "SD card unmounted successfully");
    return ESP_OK;
}

// 检查SD卡是否已挂载
bool sdcard_is_mounted(void)
{
    return (is_initialized && current_status == SD_STATUS_MOUNTED);
}

// 获取SD卡状态
sd_status_t sdcard_get_status(void)
{
    return current_status;
}

// 获取SD卡信息
esp_err_t sdcard_get_info(sd_card_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "Info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取卡基本信息
    info->sector_size = card->csd.sector_size;
    info->sector_count = card->csd.capacity;
    info->total_bytes = ((uint64_t)card->csd.capacity) * card->csd.sector_size;
    info->is_mounted = true;
    info->status = current_status;
    
    // 复制卡名称
    snprintf(info->card_name, sizeof(info->card_name), "%s", card->cid.name);
    
    // 简化的使用信息（无法在ESP-IDF中准确获取文件系统使用情况）
    info->used_bytes = 0;  // 无法精确计算已使用空间
    
    ESP_LOGI(TAG, "SD card info retrieved successfully");
    
    return ESP_OK;
}

// 格式化SD卡
esp_err_t sdcard_format(void)
{
    ESP_LOGW(TAG, "SD card formatting is not implemented in this example");
    ESP_LOGW(TAG, "Use appropriate tools to format the SD card as FAT32");
    return ESP_ERR_NOT_SUPPORTED;
}

// 创建测试文件
esp_err_t sdcard_create_test_file(const char *filename, const char *content)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, filename);
    
    ESP_LOGI(TAG, "Creating file: %s", full_path);
    
    FILE *f = fopen(full_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return ESP_FAIL;
    }
    
    fprintf(f, "%s", content);
    fclose(f);
    
    ESP_LOGI(TAG, "File created successfully");
    return ESP_OK;
}

// 读取文件内容
esp_err_t sdcard_read_file(const char *filename, char *buffer, size_t buffer_size)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, filename);
    
    ESP_LOGI(TAG, "Reading file: %s", full_path);
    
    FILE *f = fopen(full_path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, f);
    buffer[bytes_read] = '\0';  // 确保字符串结束
    
    fclose(f);
    
    ESP_LOGI(TAG, "Read %d bytes from file", bytes_read);
    return ESP_OK;
}

// 列出SD卡根目录内容
esp_err_t sdcard_list_files(void)
{
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Listing files in %s:", SD_MOUNT_POINT);
    
    DIR *d = opendir(SD_MOUNT_POINT);
    if (!d) {
        ESP_LOGE(TAG, "Failed to open directory");
        return ESP_FAIL;
    }
    
    struct dirent *dir;
    int file_count = 0;
    
    while ((dir = readdir(d)) != NULL) {
        char full_path[300];
        snprintf(full_path, sizeof(full_path), "%s/%s", SD_MOUNT_POINT, dir->d_name);
        
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                ESP_LOGI(TAG, "  [DIR]  %s", dir->d_name);
            } else {
                char size_str[32];
                sdcard_format_size(file_stat.st_size, size_str, sizeof(size_str));
                ESP_LOGI(TAG, "  [FILE] %s (%s)", dir->d_name, size_str);
            }
            file_count++;
        }
    }
    
    closedir(d);
    ESP_LOGI(TAG, "Total items: %d", file_count);
    
    return ESP_OK;
}

// 测试SD卡读写功能
esp_err_t sdcard_test_rw(void)
{
    ESP_LOGI(TAG, "Testing SD card read/write functionality");
    
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 测试写入
    const char *test_filename = "test.txt";
    const char *test_content = "Hello, SD Card!\nThis is a test file created by ESP32-S3.\nCurrent timestamp: ";
    
    // 添加时间戳
    char full_content[256];
    snprintf(full_content, sizeof(full_content), "%s%llu ms\n", test_content, esp_timer_get_time() / 1000);
    
    esp_err_t ret = sdcard_create_test_file(test_filename, full_content);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write test失败");
        return ret;
    }
    
    // 测试读取
    char read_buffer[512];
    ret = sdcard_read_file(test_filename, read_buffer, sizeof(read_buffer));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read test失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "文件内容:");
    ESP_LOGI(TAG, "%s", read_buffer);
    
    // 验证内容
    if (strstr(read_buffer, "Hello, SD Card!") != NULL) {
        ESP_LOGI(TAG, "SD卡读写测试通过");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "SD卡读写测试失败 - 内容不匹配");
        return ESP_FAIL;
    }
}


// 解释SDIO总线速度决定因素
void sdcard_explain_speed_factors(void)
{
    ESP_LOGI(TAG, "=== SDIO总线速度决定因素 ===");
    
    ESP_LOGI(TAG, "1. ESP32-S3 SDMMC控制器限制:");
    ESP_LOGI(TAG, "   - SDMMC_FREQ_PROBING: %d kHz (初始化时的低速)", SDMMC_FREQ_PROBING);
    ESP_LOGI(TAG, "   - SDMMC_FREQ_DEFAULT: %d kHz (默认速度)", SDMMC_FREQ_DEFAULT);
    ESP_LOGI(TAG, "   - SDMMC_FREQ_HIGHSPEED: %d kHz (高速模式)", SDMMC_FREQ_HIGHSPEED);
    
    ESP_LOGI(TAG, "2. SD卡速度等级:");
    ESP_LOGI(TAG, "   - Class 2: 最低2MB/s持续写入");
    ESP_LOGI(TAG, "   - Class 4: 最低4MB/s持续写入");
    ESP_LOGI(TAG, "   - Class 6: 最低6MB/s持续写入");
    ESP_LOGI(TAG, "   - Class 10: 最低10MB/s持续写入");
    ESP_LOGI(TAG, "   - UHS-I U1: 最低10MB/s持续写入");
    ESP_LOGI(TAG, "   - UHS-I U3: 最低30MB/s持续写入");
    
    ESP_LOGI(TAG, "3. 总线宽度影响:");
    ESP_LOGI(TAG, "   - 1位模式: 单线传输");
    ESP_LOGI(TAG, "   - 4位模式: 4倍数据传输能力");
    
    ESP_LOGI(TAG, "4. 实际速度计算:");
    ESP_LOGI(TAG, "   理论传输速度 = 频率 × 总线宽度 ÷ 8");
    ESP_LOGI(TAG, "   例如: 40MHz × 4位 ÷ 8 = 20MB/s");
    
    ESP_LOGI(TAG, "5. 影响因素:");
    ESP_LOGI(TAG, "   - PCB走线长度和质量");
    ESP_LOGI(TAG, "   - 电源噪声");
    ESP_LOGI(TAG, "   - GPIO驱动能力");
    ESP_LOGI(TAG, "   - SD卡本身的性能");
}

// 测试不同SDIO速度设置
esp_err_t sdcard_test_different_speeds(void)
{
    ESP_LOGI(TAG, "=== 测试不同SDIO速度设置 ===");
    
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 显示当前速度
    ESP_LOGI(TAG, "当前SDIO配置:");
    ESP_LOGI(TAG, "  实际运行频率: %d kHz (%d MHz)", card->real_freq_khz, card->real_freq_khz / 1000);
    ESP_LOGI(TAG, "  最大支持频率: %d kHz (%d MHz)", card->max_freq_khz, card->max_freq_khz / 1000);
    
    // 理论传输速度计算
    int bus_width = (card->host.flags & SDMMC_HOST_FLAG_4BIT) ? 4 : 1;
    float theoretical_speed_mbps = (float)(card->real_freq_khz * bus_width) / 8000.0f;
    
    ESP_LOGI(TAG, "  总线宽度: %d 位", bus_width);
    ESP_LOGI(TAG, "  理论传输速度: %.2f MB/s", theoretical_speed_mbps);
    
    // 简单的写入性能测试
    ESP_LOGI(TAG, "执行简单的写入性能测试...");
    
    const char *test_file = "speed_test.txt";
    char test_path[64];
    snprintf(test_path, sizeof(test_path), "%s/%s", SD_MOUNT_POINT, test_file);
    
    // 记录开始时间
    int64_t start_time = esp_timer_get_time();
    
    // 写入1KB数据
    FILE *f = fopen(test_path, "w");
    if (f) {
        for (int i = 0; i < 64; i++) {
            fprintf(f, "This is line %d for speed testing\n", i);
        }
        fclose(f);
        
        // 记录结束时间
        int64_t end_time = esp_timer_get_time();
        int64_t duration_us = end_time - start_time;
        float duration_ms = duration_us / 1000.0f;
        
        ESP_LOGI(TAG, "写入测试完成:");
        ESP_LOGI(TAG, "  耗时: %.2f ms", duration_ms);
        ESP_LOGI(TAG, "  写入速度约: %.2f KB/s", 1024.0f / (duration_ms / 1000.0f));
    } else {
        ESP_LOGE(TAG, "无法创建测试文件");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
