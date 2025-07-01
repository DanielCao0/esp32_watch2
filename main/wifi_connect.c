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
#include "clock.h"  // 包含时钟相关函数

#define WIFI_SSID      "RAK"
#define WIFI_PASS      "rak20140629"

#define HOME_SSID      "HONOR-0F19KY_2G4"
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
static const char* get_disconnect_reason_string(uint8_t reason);

// 获取WiFi断开原因的字符串描述
static const char* get_disconnect_reason_string(uint8_t reason)
{
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "Unspecified";
        case WIFI_REASON_AUTH_EXPIRE: return "Auth expired";
        case WIFI_REASON_AUTH_LEAVE: return "Auth leave";
        case WIFI_REASON_ASSOC_EXPIRE: return "Assoc expired";
        case WIFI_REASON_ASSOC_TOOMANY: return "Too many associations";
        case WIFI_REASON_NOT_AUTHED: return "Not authenticated";
        case WIFI_REASON_NOT_ASSOCED: return "Not associated";
        case WIFI_REASON_ASSOC_LEAVE: return "Assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "Assoc not authenticated";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "Bad power capability";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "Bad supported channels";
        case WIFI_REASON_IE_INVALID: return "Invalid IE";
        case WIFI_REASON_MIC_FAILURE: return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "Group key update timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE in 4-way differs";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "Invalid group cipher";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "Invalid pairwise cipher";
        case WIFI_REASON_AKMP_INVALID: return "Invalid AKMP";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "Unsupported RSN IE version";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "Invalid RSN IE cap";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802.1x auth failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "Cipher suite rejected";
        case WIFI_REASON_BEACON_TIMEOUT: return "Beacon timeout";
        case WIFI_REASON_NO_AP_FOUND: return "No AP found";
        case WIFI_REASON_AUTH_FAIL: return "Auth failed";
        case WIFI_REASON_ASSOC_FAIL: return "Assoc failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL: return "Connection failed";
        case WIFI_REASON_AP_TSF_RESET: return "AP TSF reset";
        case WIFI_REASON_ROAMING: return "Roaming";
        default: return "Unknown";
    }
}

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
    
    // 更新WiFi配置，使用更宽松的设置
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,  // 使用更宽松的认证模式
            .pmf_cfg = {
                .capable = true,
                .required = false  // PMF不强制要求
            },
        },
    };
    
    strcpy((char*)wifi_config.sta.ssid, wifi_configs[current_wifi_index].ssid);
    strcpy((char*)wifi_config.sta.password, wifi_configs[current_wifi_index].password);
    
    ESP_LOGI(TAG, "WiFi config details - SSID: '%s', Password length: %d", 
             wifi_config.sta.ssid, strlen((char*)wifi_config.sta.password));
    
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
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "WiFi disconnected - SSID: %s, Reason: %d (%s)", 
                 wifi_configs[current_wifi_index].ssid, disconnected->reason,
                 get_disconnect_reason_string(disconnected->reason));
        
        if (is_connecting) {
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

        obtain_beijing_time()  ;
    }
}

void wifi_connect_init(void)
{
    ESP_LOGI(TAG, "Starting WiFi initialization...");
    
    // ========================= NVS Flash 初始化 =========================
    // NVS (Non-Volatile Storage) 用于存储WiFi配置等持久化数据
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Flash initialized successfully");

    // ========================= 添加启动延时，确保系统稳定 =========================
    ESP_LOGI(TAG, "Waiting for system to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒让系统稳定

    // ========================= 确保WiFi完全停止（清理之前的状态） =========================
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(500));  // 等待WiFi完全停止

    // ========================= 事件组创建 =========================
    // 创建事件组，用于在不同任务间同步WiFi连接状态
    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);  // 清理旧的事件组
    }
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return;
    }

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
    ESP_LOGI(TAG, "WiFi driver initialized");

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
    ESP_LOGI(TAG, "Event handlers registered");

    // ========================= WiFi 配置设置 =========================
    // 创建WiFi配置结构体，使用更宽松的认证设置
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,  // 使用更宽松的认证模式
            .pmf_cfg = {
                .capable = true,
                .required = false  // PMF不强制要求
            },
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,  // 按信号强度排序
        },
    };
    
    // 使用第一个WiFi配置（RAK网络）
    strcpy((char*)wifi_config.sta.ssid, wifi_configs[0].ssid);
    strcpy((char*)wifi_config.sta.password, wifi_configs[0].password);
    current_wifi_index = 0;      // 当前使用第一个WiFi配置
    attempts_in_cycle = 0;       // 初始化当前周期的尝试次数
    is_connecting = false;       // 重置连接状态
    
    ESP_LOGI(TAG, "Initial WiFi config - SSID: '%s', Password length: %d", 
             wifi_config.sta.ssid, strlen((char*)wifi_config.sta.password));
    
    // ========================= WiFi 模式和配置应用 =========================
    // 设置WiFi为Station模式（客户端模式）
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 应用WiFi配置到驱动
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // 启动WiFi驱动
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started in station mode");

    // ========================= 等待WiFi驱动完全启动 =========================
    ESP_LOGI(TAG, "Waiting for WiFi driver to be ready...");
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待1秒让WiFi驱动完全启动

    // ========================= 定时器创建和管理 =========================
    // 清理旧的定时器
    if (wifi_timeout_timer != NULL) {
        xTimerDelete(wifi_timeout_timer, portMAX_DELAY);
        wifi_timeout_timer = NULL;
    }
    if (wifi_retry_timer != NULL) {
        xTimerDelete(wifi_retry_timer, portMAX_DELAY);
        wifi_retry_timer = NULL;
    }

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
    ESP_LOGI(TAG, "WiFi timers created successfully");

    // ========================= 初始化完成日志 =========================
    // 打印初始化完成信息
    ESP_LOGI(TAG, "WiFi init finished. Will attempt connection to %d WiFi networks every 15 minutes with 30s timeout.", MAX_WIFI_CONFIGS);

    // ========================= 延迟开始首次连接尝试 =========================
    // 不立即连接，给系统更多时间稳定
    ESP_LOGI(TAG, "Scheduling first connection attempt in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));  // 再等待3秒
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

// 扫描可用的WiFi网络
void wifi_scan_networks(void)
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        ESP_LOGW(TAG, "No WiFi networks found");
        return;
    }
    
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_info == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return;
    }
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_info);
    
    ESP_LOGI(TAG, "Found %d WiFi networks:", ap_count);
    for (int i = 0; i < ap_count; i++) {
        ESP_LOGI(TAG, "  %d: SSID=%s, RSSI=%d, Auth=%d, Channel=%d", 
                 i+1, ap_info[i].ssid, ap_info[i].rssi, ap_info[i].authmode, ap_info[i].primary);
        
        // 检查是否是我们配置的网络
        for (int j = 0; j < MAX_WIFI_CONFIGS; j++) {
            if (strcmp((char*)ap_info[i].ssid, wifi_configs[j].ssid) == 0) {
                ESP_LOGI(TAG, "    -> This matches our config %d!", j);
            }
        }
    }
    
    free(ap_info);
}

// 尝试更宽松的认证模式
void wifi_try_relaxed_auth(void)
{
    ESP_LOGI(TAG, "Trying relaxed authentication settings...");
    
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,  // 更宽松的认证模式
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    strcpy((char*)wifi_config.sta.ssid, wifi_configs[current_wifi_index].ssid);
    strcpy((char*)wifi_config.sta.password, wifi_configs[current_wifi_index].password);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Applied relaxed auth config for SSID: %s", wifi_configs[current_wifi_index].ssid);
}

// WiFi诊断功能
void wifi_diagnose(void)
{
    ESP_LOGI(TAG, "=== WiFi Diagnostic Information ===");
    
    // 打印当前配置
    ESP_LOGI(TAG, "Current WiFi configurations:");
    for (int i = 0; i < MAX_WIFI_CONFIGS; i++) {
        ESP_LOGI(TAG, "  Config %d: SSID='%s', Password length=%d", 
                 i, wifi_configs[i].ssid, strlen(wifi_configs[i].password));
    }
    
    ESP_LOGI(TAG, "Current index: %d, Attempts in cycle: %d", 
             current_wifi_index, attempts_in_cycle);
    ESP_LOGI(TAG, "Connection status: %s", wifi_is_connected() ? "Connected" : "Disconnected");
    ESP_LOGI(TAG, "Is connecting: %s", is_connecting ? "Yes" : "No");
    
    // 扫描网络
    wifi_scan_networks();
    
    ESP_LOGI(TAG, "=== End WiFi Diagnostic ===");
}

// 完全重置WiFi状态（解决间歇性连接问题）
void wifi_complete_reset(void)
{
    ESP_LOGI(TAG, "Performing complete WiFi reset...");
    
    // 停止所有定时器
    if (wifi_timeout_timer != NULL) {
        xTimerStop(wifi_timeout_timer, portMAX_DELAY);
    }
    if (wifi_retry_timer != NULL) {
        xTimerStop(wifi_retry_timer, portMAX_DELAY);
    }
    
    // 断开WiFi连接
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 停止WiFi
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 重置状态变量
    is_connecting = false;
    attempts_in_cycle = 0;
    current_wifi_index = 0;
    
    // 清除事件组位
    if (wifi_event_group != NULL) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
    
    // 重新启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待WiFi重新启动
    
    ESP_LOGI(TAG, "WiFi reset completed, ready for new connection attempt");
}

// 智能重连（根据失败次数调整策略）
void wifi_smart_reconnect(void)
{
    static int consecutive_failures = 0;
    
    ESP_LOGI(TAG, "Smart reconnect - consecutive failures: %d", consecutive_failures);
    
    if (consecutive_failures >= 3) {
        ESP_LOGW(TAG, "Multiple failures detected, performing complete reset");
        wifi_complete_reset();
        consecutive_failures = 0;
    } else {
        consecutive_failures++;
        wifi_reconnect();
    }
}