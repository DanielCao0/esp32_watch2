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

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONNECT_H
