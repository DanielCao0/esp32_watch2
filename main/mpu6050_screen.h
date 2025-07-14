/**
 * MPU6050 3D Visualization Screen
 * Shows real-time 3D cube rotation based on MPU6050 orientation data
 * Author: GitHub Copilot
 * Date: 2025-01-21
 */

#ifndef MPU6050_SCREEN_H
#define MPU6050_SCREEN_H

#include "lvgl.h"
#include "mpu6050.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create MPU6050 visualization screen
 * 
 * Creates a screen that displays:
 * - 3D cube showing orientation
 * - Real-time sensor data
 * - Statistics and status
 * 
 * @param parent Parent object (usually screen)
 * @return lv_obj_t* Pointer to the created screen container
 */
lv_obj_t* mpu6050_screen_create(lv_obj_t* parent);

/**
 * @brief Update the MPU6050 screen with new sensor data
 * 
 * @param screen MPU6050 screen object
 * @param data Latest MPU6050 sensor data
 */
void mpu6050_screen_update(lv_obj_t* screen, const mpu6050_data_t* data);

/**
 * @brief Show/hide the MPU6050 screen
 * 
 * @param screen MPU6050 screen object
 * @param show true to show, false to hide
 */
void mpu6050_screen_set_visible(lv_obj_t* screen, bool show);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_SCREEN_H
