/**
 * LVGL 3D Cube Display for MPU6050 Orientation Visualization
 * Shows a rotating 3D cube based on accelerometer roll/pitch data
 * Author: GitHub Copilot
 * Date: 2025-01-21
 */

#ifndef LVGL_3D_CUBE_H
#define LVGL_3D_CUBE_H

#include "lvgl.h"
#include "mpu6050.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 3D Point structure
 */
typedef struct {
    float x, y, z;
} point_3d_t;

/**
 * @brief 2D Point structure for screen projection
 */
typedef struct {
    int16_t x, y;
} point_2d_t;

/**
 * @brief Create a 3D cube widget
 * 
 * @param parent Parent object
 * @param size Size of the cube (in pixels)
 * @return lv_obj_t* Pointer to the created cube object
 */
lv_obj_t* lv_3d_cube_create(lv_obj_t* parent, int16_t size);

/**
 * @brief Update cube orientation from MPU6050 data
 * 
 * @param cube Cube object
 * @param data MPU6050 sensor data
 */
void lv_3d_cube_update_orientation(lv_obj_t* cube, const mpu6050_data_t* data);

/**
 * @brief Set cube rotation angles manually
 * 
 * @param cube Cube object
 * @param roll Roll angle in degrees
 * @param pitch Pitch angle in degrees
 */
void lv_3d_cube_set_rotation(lv_obj_t* cube, float roll, float pitch);

/**
 * @brief Set colors for cube faces
 * 
 * @param cube Cube object
 * @param colors Array of 6 colors for faces (front, back, left, right, top, bottom)
 */
void lv_3d_cube_set_colors(lv_obj_t* cube, const lv_color_t colors[6]);

#ifdef __cplusplus
}
#endif

#endif // LVGL_3D_CUBE_H
