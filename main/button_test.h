/**
 * @file button_test.h
 * @brief BOOT按钮测试功能头文件
 */

#ifndef BUTTON_TEST_H
#define BUTTON_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化按钮测试
 * 初始化按钮驱动并设置监控任务
 */
void init_button_test(void);

/**
 * @brief 打印按钮状态信息（调试用）
 */
void print_button_status(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_TEST_H */
