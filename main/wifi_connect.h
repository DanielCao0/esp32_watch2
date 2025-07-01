#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化WiFi连接，设置15分钟重试机制和30秒超时
 */
void wifi_connect_init(void);

/**
 * @brief 检查WiFi是否已连接
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief 手动触发WiFi重连
 */
void wifi_reconnect(void);

/**
 * @brief 获取当前连接的WiFi SSID
 * @return 当前连接的WiFi名称，如果未连接则返回"Not Connected"
 */
const char* wifi_get_current_ssid(void);

/**
 * @brief 扫描可用的WiFi网络并打印详细信息
 */
void wifi_scan_networks(void);

/**
 * @brief 尝试使用更宽松的认证模式连接当前WiFi
 */
void wifi_try_relaxed_auth(void);

/**
 * @brief WiFi诊断功能，打印详细的连接信息和扫描结果
 */
void wifi_diagnose(void);

/**
 * @brief 完全重置WiFi状态，解决间歇性连接问题
 */
void wifi_complete_reset(void);

/**
 * @brief 智能重连，根据失败次数自动调整策略
 */
void wifi_smart_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONNECT_H
