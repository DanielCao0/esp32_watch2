#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>

#define WIFI_SSID      "RAK"
#define WIFI_PASS      "rak20140629"

#define HOME_SSID      "HONOR-0F19KY_2.4G"
#define HOME_PASS      "syqcy1314!"

#define MAX_WIFI_CONFIGS 2  // 支持的WiFi配置数量

// WiFi配置结构
typedef struct {
    char ssid[32];
    char password[64];
} wifi_config_entry_t;

// WiFi配置列表
static wifi_config_entry_t wifi_configs[MAX_WIFI_CONFIGS] = {
    {WIFI_SSID, WIFI_PASS},
    {HOME_SSID, HOME_PASS}
};

static int current_wifi_index = 0;  // 当前尝试的WiFi配置索引
static int attempts_in_cycle = 0;   // 当前周期内的尝试次数

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_TIMEOUT_MS (30 * 1000)    // 30秒连接超时
#define WIFI_RETRY_INTERVAL_MS (15 * 60 * 1000)  // 15分钟重试间隔

static EventGroupHandle_t wifi_event_group;
static TimerHandle_t wifi_retry_timer = NULL;
static TimerHandle_t wifi_timeout_timer = NULL;
static const char *TAG = "wifi_connect";
static bool is_connecting = false;

// 函数前向声明
static void wifi_start_connection(void);
static void try_next_wifi_config(void);
static void wifi_timeout_callback(TimerHandle_t timer);
static void wifi_retry_callback(TimerHandle_t timer);

// WiFi连接超时回调
static void wifi_timeout_callback(TimerHandle_t timer)
{
    if (is_connecting) {
        ESP_LOGW(TAG, "WiFi connection timeout for SSID: %s", wifi_configs[current_wifi_index].ssid);
        esp_wifi_disconnect();
        is_connecting = false;
        
        attempts_in_cycle++;
        
        // 检查是否已经尝试了所有WiFi配置
        if (attempts_in_cycle >= MAX_WIFI_CONFIGS) {
            ESP_LOGI(TAG, "All WiFi configurations tried, waiting 15 minutes before next cycle");
            attempts_in_cycle = 0;
            current_wifi_index = 0;  // 重置到第一个配置
            
            // 启动15分钟重试定时器
            if (wifi_retry_timer != NULL) {
                xTimerStart(wifi_retry_timer, 0);
            }
        } else {
            // 尝试下一个WiFi配置
            try_next_wifi_config();
        }
    }
}

// WiFi重试定时器回调
static void wifi_retry_callback(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Starting new WiFi connection cycle, trying all %d configurations", MAX_WIFI_CONFIGS);
    attempts_in_cycle = 0;
    current_wifi_index = 0;  // 从第一个配置开始
    wifi_start_connection();
}

// 启动WiFi连接尝试
static void wifi_start_connection(void)
{
    if (is_connecting) {
        ESP_LOGW(TAG, "WiFi connection already in progress");
        return;
    }
    
    is_connecting = true;
    ESP_LOGI(TAG, "Starting WiFi connection attempt for SSID: %s", wifi_configs[current_wifi_index].ssid);
    
    // 启动超时定时器
    if (wifi_timeout_timer != NULL) {
        xTimerStart(wifi_timeout_timer, 0);
    }
    
    esp_wifi_connect();
}

// 尝试下一个WiFi配置
static void try_next_wifi_config(void)
{
    // 切换到下一个WiFi配置
    current_wifi_index = (current_wifi_index + 1) % MAX_WIFI_CONFIGS;
    
    ESP_LOGI(TAG, "Trying WiFi config %d: SSID=%s (attempt %d/%d in this cycle)", 
             current_wifi_index, wifi_configs[current_wifi_index].ssid, 
             attempts_in_cycle + 1, MAX_WIFI_CONFIGS);
    
    // 更新WiFi配置
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strcpy((char*)wifi_config.sta.ssid, wifi_configs[current_wifi_index].ssid);
    strcpy((char*)wifi_config.sta.password, wifi_configs[current_wifi_index].password);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 立即开始连接
    wifi_start_connection();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        // 不在这里自动连接，由定时器控制
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (is_connecting) {
            ESP_LOGW(TAG, "WiFi disconnected during connection attempt for SSID: %s", 
                     wifi_configs[current_wifi_index].ssid);
            is_connecting = false;
            
            // 停止超时定时器
            if (wifi_timeout_timer != NULL) {
                xTimerStop(wifi_timeout_timer, 0);
            }
            
            attempts_in_cycle++;
            
            // 检查是否已经尝试了所有WiFi配置
            if (attempts_in_cycle >= MAX_WIFI_CONFIGS) {
                ESP_LOGI(TAG, "All WiFi configurations tried, waiting 15 minutes before next cycle");
                attempts_in_cycle = 0;
                current_wifi_index = 0;  // 重置到第一个配置
                
                // 启动15分钟重试定时器
                if (wifi_retry_timer != NULL) {
                    xTimerStart(wifi_retry_timer, 0);
                }
            } else {
                // 尝试下一个WiFi配置
                try_next_wifi_config();
            }
        } else {
            ESP_LOGI(TAG, "WiFi disconnected, will retry in 15 minutes");
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            
            // 重置计数器，从第一个配置开始新的周期
            attempts_in_cycle = 0;
            current_wifi_index = 0;
            
            // 启动重试定时器
            if (wifi_retry_timer != NULL) {
                xTimerStart(wifi_retry_timer, 0);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected to SSID: %s, Got IP: " IPSTR " (attempt %d/%d)", 
                 wifi_configs[current_wifi_index].ssid, IP2STR(&event->ip_info.ip),
                 attempts_in_cycle + 1, MAX_WIFI_CONFIGS);
        
        is_connecting = false;
        attempts_in_cycle = 0;  // 重置尝试计数
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        
        // 停止所有定时器
        if (wifi_timeout_timer != NULL) {
            xTimerStop(wifi_timeout_timer, 0);
        }
        if (wifi_retry_timer != NULL) {
            xTimerStop(wifi_retry_timer, 0);
        }
    }
}

void wifi_connect_init(void)
{
    // ========================= NVS Flash 初始化 =========================
    // NVS (Non-Volatile Storage) 用于存储WiFi配置等持久化数据
    esp_err_t ret = nvs_flash_init();
    ESP_ERROR_CHECK(ret);

    // ========================= 事件组创建 =========================
    // 创建事件组，用于在不同任务间同步WiFi连接状态
    wifi_event_group = xEventGroupCreate();

    // ========================= 网络接口初始化 =========================
    // 初始化TCP/IP网络协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认事件循环，用于处理系统事件
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 创建默认的WiFi Station网络接口
    esp_netif_create_default_wifi_sta();

    // ========================= WiFi 驱动初始化 =========================
    // 使用默认配置初始化WiFi驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ========================= 事件处理器注册 =========================
    // 注册WiFi事件处理器，监听所有WiFi相关事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    // 注册IP事件处理器，监听获取IP地址事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // ========================= WiFi 配置设置 =========================
    // 创建WiFi配置结构体
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // 设置最低安全模式为WPA2
        },
    };
    
    // 使用第一个WiFi配置（RAK网络）
    strcpy((char*)wifi_config.sta.ssid, wifi_configs[0].ssid);
    strcpy((char*)wifi_config.sta.password, wifi_configs[0].password);
    current_wifi_index = 0;      // 当前使用第一个WiFi配置
    attempts_in_cycle = 0;       // 初始化当前周期的尝试次数
    
    // ========================= WiFi 模式和配置应用 =========================
    // 设置WiFi为Station模式（客户端模式）
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 应用WiFi配置到驱动
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // 启动WiFi驱动
    ESP_ERROR_CHECK(esp_wifi_start());

    // ========================= 定时器创建 =========================
    // 创建WiFi连接超时定时器（30秒）
    wifi_timeout_timer = xTimerCreate("wifi_timeout",
                                     pdMS_TO_TICKS(WIFI_TIMEOUT_MS),
                                     pdFALSE,  // 单次触发，不自动重载
                                     NULL,
                                     wifi_timeout_callback);

    // 创建WiFi重试定时器（15分钟）
    wifi_retry_timer = xTimerCreate("wifi_retry",
                                   pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS),
                                   pdFALSE,  // 单次触发，不自动重载
                                   NULL,
                                   wifi_retry_callback);

    // ========================= 定时器创建检查 =========================
    // 检查定时器是否创建成功
    if (wifi_timeout_timer == NULL || wifi_retry_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi timers");
        return;
    }

    // ========================= 初始化完成日志 =========================
    // 打印初始化完成信息
    ESP_LOGI(TAG, "WiFi init finished. Will attempt connection to %d WiFi networks every 15 minutes with 30s timeout.", MAX_WIFI_CONFIGS);

    // ========================= 开始首次连接尝试 =========================
    // 立即开始第一次WiFi连接尝试
    wifi_start_connection();
}

// 检查WiFi是否已连接
bool wifi_is_connected(void)
{
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

// 手动触发WiFi重连
void wifi_reconnect(void)
{
    ESP_LOGI(TAG, "Manual WiFi reconnection triggered");
    if (!is_connecting) {
        attempts_in_cycle = 0;  // 重置计数器
        current_wifi_index = 0; // 从第一个配置开始
        wifi_start_connection();
    } else {
        ESP_LOGW(TAG, "WiFi connection already in progress");
    }
}

// 获取当前连接的WiFi SSID
const char* wifi_get_current_ssid(void)
{
    if (wifi_is_connected()) {
        return wifi_configs[current_wifi_index].ssid;
    }
    return "Not Connected";
}