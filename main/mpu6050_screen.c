/**
 * MPU6050 3D Visualization Screen
 * Shows real-time 3D cube rotation based on MPU6050 orientation data
 * Author: GitHub Copilot
 * Date: 2025-01-21
 */

#include "mpu6050_screen.h"
#include "lvgl_3d_cube.h"
#include "esp_log.h"
#include "ui.h"
#include <stdio.h>

static const char *TAG = "MPU6050_SCREEN";

// Screen elements
typedef struct {
    lv_obj_t* container;
    lv_obj_t* cube_3d;
    lv_obj_t* data_panel;
    lv_obj_t* accel_label;
    lv_obj_t* gyro_label;
    lv_obj_t* angle_label;
    lv_obj_t* temp_label;
    lv_obj_t* status_label;
} mpu6050_screen_t;

static mpu6050_screen_t screen_data = {0};

/**
 * Back button event handler
 */
static void back_button_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Return to the main screen
        if (ui_Screen1 != NULL) {
            lv_screen_load(ui_Screen1);
        }
    }
}

/**
 * Create data display panel
 */
static lv_obj_t* create_data_panel(lv_obj_t* parent)
{
    // Create panel container
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 200, 180);
    lv_obj_align(panel, LV_ALIGN_RIGHT_MID, -10, 0);
    
    // Panel styling
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x404040), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    
    // Title
    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "MPU6050 Data");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // Accelerometer data
    screen_data.accel_label = lv_label_create(panel);
    lv_label_set_text(screen_data.accel_label, "Accel: ---, ---, ---");
    lv_obj_set_style_text_color(screen_data.accel_label, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_text_font(screen_data.accel_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_data.accel_label, LV_ALIGN_TOP_LEFT, 0, 25);
    
    // Gyroscope data
    screen_data.gyro_label = lv_label_create(panel);
    lv_label_set_text(screen_data.gyro_label, "Gyro: ---, ---, ---");
    lv_obj_set_style_text_color(screen_data.gyro_label, lv_color_hex(0x0080ff), 0);
    lv_obj_set_style_text_font(screen_data.gyro_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_data.gyro_label, LV_ALIGN_TOP_LEFT, 0, 45);
    
    // Angle data
    screen_data.angle_label = lv_label_create(panel);
    lv_label_set_text(screen_data.angle_label, "Roll: ---°  Pitch: ---°");
    lv_obj_set_style_text_color(screen_data.angle_label, lv_color_hex(0xff8000), 0);
    lv_obj_set_style_text_font(screen_data.angle_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_data.angle_label, LV_ALIGN_TOP_LEFT, 0, 65);
    
    // Temperature data
    screen_data.temp_label = lv_label_create(panel);
    lv_label_set_text(screen_data.temp_label, "Temp: ---°C");
    lv_obj_set_style_text_color(screen_data.temp_label, lv_color_hex(0xff4080), 0);
    lv_obj_set_style_text_font(screen_data.temp_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_data.temp_label, LV_ALIGN_TOP_LEFT, 0, 85);
    
    // Status info
    screen_data.status_label = lv_label_create(panel);
    lv_label_set_text(screen_data.status_label, "Status: Initializing...");
    lv_obj_set_style_text_color(screen_data.status_label, lv_color_hex(0xc0c0c0), 0);
    lv_obj_set_style_text_font(screen_data.status_label, &lv_font_montserrat_10, 0);
    lv_obj_align(screen_data.status_label, LV_ALIGN_TOP_LEFT, 0, 110);
    
    // Instructions
    lv_obj_t* instruction = lv_label_create(panel);
    lv_label_set_text(instruction, "Tilt the watch to\nsee cube rotation");
    lv_obj_set_style_text_color(instruction, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(instruction, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(instruction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    return panel;
}

/**
 * Create MPU6050 visualization screen
 */
lv_obj_t* mpu6050_screen_create(lv_obj_t* parent)
{
    // Create independent screen if parent is NULL
    if (parent == NULL) {
        screen_data.container = lv_obj_create(NULL);  // Create as independent screen
    } else {
        screen_data.container = lv_obj_create(parent);
    }
    
    lv_obj_set_size(screen_data.container, LV_PCT(100), LV_PCT(100));
    
    // Container styling
    lv_obj_set_style_bg_color(screen_data.container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_data.container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen_data.container, 0, 0);
    lv_obj_set_style_pad_all(screen_data.container, 0, 0);
    
    // Create title
    lv_obj_t* title = lv_label_create(screen_data.container);
    lv_label_set_text(title, "MPU6050 3D Orientation");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    
    // Create back button
    lv_obj_t* back_btn = lv_btn_create(screen_data.container);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_button_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Create 3D cube
    screen_data.cube_3d = lv_3d_cube_create(screen_data.container, 60);
    lv_obj_align(screen_data.cube_3d, LV_ALIGN_LEFT_MID, 20, 0);
    
    // Create data display panel
    screen_data.data_panel = create_data_panel(screen_data.container);
    
    ESP_LOGI(TAG, "MPU6050 visualization screen created");
    return screen_data.container;
}

/**
 * Update the MPU6050 screen with new sensor data
 */
void mpu6050_screen_update(lv_obj_t* screen, const mpu6050_data_t* data)
{
    if (screen == NULL || data == NULL) {
        ESP_LOGW(TAG, "Invalid parameters for screen update");
        return;
    }
    
    // Update screen with new data
    
    // Update 3D cube orientation
    if (screen_data.cube_3d != NULL) {
        lv_3d_cube_update_orientation(screen_data.cube_3d, data);
    } else {
        ESP_LOGW(TAG, "3D cube is NULL");
    }
    
    // Update text labels using sprintf for better float support
    if (screen_data.accel_label != NULL) {
        char accel_text[64];
        sprintf(accel_text, "Accel: %.2f, %.2f, %.2f g", 
                data->accel_x, data->accel_y, data->accel_z);
        lv_label_set_text(screen_data.accel_label, accel_text);
    } else {
        ESP_LOGW(TAG, "Accel label is NULL");
    }
    
    if (screen_data.gyro_label != NULL) {
        char gyro_text[64];
        sprintf(gyro_text, "Gyro: %.1f, %.1f, %.1f °/s",
                data->gyro_x, data->gyro_y, data->gyro_z);
        lv_label_set_text(screen_data.gyro_label, gyro_text);
    } else {
        ESP_LOGW(TAG, "Gyro label is NULL");
    }
    
    if (screen_data.angle_label != NULL) {
        char angle_text[64];
        sprintf(angle_text, "Roll: %.1f°  Pitch: %.1f°",
                data->roll, data->pitch);
        lv_label_set_text(screen_data.angle_label, angle_text);
    } else {
        ESP_LOGW(TAG, "Angle label is NULL");
    }
    
    if (screen_data.temp_label != NULL) {
        char temp_text[32];
        sprintf(temp_text, "Temp: %.1f°C", data->temperature);
        lv_label_set_text(screen_data.temp_label, temp_text);
    } else {
        ESP_LOGW(TAG, "Temp label is NULL");
    }
    
    if (screen_data.status_label != NULL) {
        lv_label_set_text(screen_data.status_label, "Status: Active");
    } else {
        ESP_LOGW(TAG, "Status label is NULL");
    }
}

/**
 * Show/hide the MPU6050 screen
 */
void mpu6050_screen_set_visible(lv_obj_t* screen, bool show)
{
    if (screen_data.container == NULL) {
        ESP_LOGW(TAG, "MPU6050 screen not initialized");
        return;
    }
    
    if (show) {
        lv_obj_clear_flag(screen_data.container, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "MPU6050 screen shown");
    } else {
        lv_obj_add_flag(screen_data.container, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "MPU6050 screen hidden");
    }
}
