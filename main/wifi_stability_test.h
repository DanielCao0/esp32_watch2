/**
 * @file wifi_stability_test.h
 * @brief WiFi连接稳定性测试工具头文件
 */

#ifndef WIFI_STABILITY_TEST_H
#define WIFI_STABILITY_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi连接稳定性测试
 * 执行多次连接测试，统计成功率
 */
void wifi_stability_test(void);

/**
 * @brief 启动WiFi连续监控任务
 * 持续监控WiFi连接状态，自动重连
 */
void start_wifi_monitor(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STABILITY_TEST_H */
