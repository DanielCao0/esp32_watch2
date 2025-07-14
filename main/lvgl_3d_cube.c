/**
 * LVGL 3D Cube Display for MPU6050 Orientation Visualization
 * Shows a rotating 3D cube based on accelerometer roll/pitch data
 * Author: GitHub Copilot
 * Date: 2025-01-21
 */

#include "lvgl_3d_cube.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "3D_CUBE";

// Convert degrees to radians
#define DEG_TO_RAD(deg) ((deg) * M_PI / 180.0f)

// 3D cube data structure (stored as user data)
typedef struct {
    point_3d_t vertices[8]; // 8 vertices of the cube
    point_2d_t projected[8]; // Projected 2D points
    float roll;             // Current roll angle (degrees)
    float pitch;            // Current pitch angle (degrees)
    lv_color_t face_colors[6]; // Colors for 6 faces
    int16_t cube_size;      // Size of the cube
    int16_t center_x, center_y; // Center position
} cube_data_t;

/**
 * Initialize cube vertices (unit cube centered at origin)
 */
static void init_cube_vertices(cube_data_t* cube)
{
    float half_size = cube->cube_size / 2.0f;
    
    // Define 8 vertices of a cube
    cube->vertices[0] = (point_3d_t){-half_size, -half_size, -half_size}; // 0: front-bottom-left
    cube->vertices[1] = (point_3d_t){ half_size, -half_size, -half_size}; // 1: front-bottom-right
    cube->vertices[2] = (point_3d_t){ half_size,  half_size, -half_size}; // 2: front-top-right
    cube->vertices[3] = (point_3d_t){-half_size,  half_size, -half_size}; // 3: front-top-left
    cube->vertices[4] = (point_3d_t){-half_size, -half_size,  half_size}; // 4: back-bottom-left
    cube->vertices[5] = (point_3d_t){ half_size, -half_size,  half_size}; // 5: back-bottom-right
    cube->vertices[6] = (point_3d_t){ half_size,  half_size,  half_size}; // 6: back-top-right
    cube->vertices[7] = (point_3d_t){-half_size,  half_size,  half_size}; // 7: back-top-left
}

/**
 * Rotate a 3D point around X-axis (pitch)
 */
static point_3d_t rotate_x(const point_3d_t* p, float angle_rad)
{
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    point_3d_t result;
    result.x = p->x;
    result.y = p->y * cos_a - p->z * sin_a;
    result.z = p->y * sin_a + p->z * cos_a;
    
    return result;
}

/**
 * Rotate a 3D point around Y-axis (roll)
 */
static point_3d_t rotate_y(const point_3d_t* p, float angle_rad)
{
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    point_3d_t result;
    result.x = p->x * cos_a + p->z * sin_a;
    result.y = p->y;
    result.z = -p->x * sin_a + p->z * cos_a;
    
    return result;
}

/**
 * Rotate a 3D point around Z-axis (yaw)
 */
static point_3d_t rotate_z(const point_3d_t* p, float angle_rad)
{
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    point_3d_t result;
    result.x = p->x * cos_a - p->y * sin_a;
    result.y = p->x * sin_a + p->y * cos_a;
    result.z = p->z;
    
    return result;
}

/**
 * Project 3D point to 2D screen coordinates
 */
static point_2d_t project_to_2d(const point_3d_t* p, int16_t center_x, int16_t center_y)
{
    // Simple orthographic projection (no perspective)
    point_2d_t result;
    result.x = center_x + (int16_t)p->x;
    result.y = center_y + (int16_t)p->y;
    
    return result;
}

/**
 * Calculate transformed and projected vertices
 */
static void calculate_projected_vertices(cube_data_t* cube)
{
    float roll_rad = DEG_TO_RAD(cube->roll);
    float pitch_rad = DEG_TO_RAD(cube->pitch);
    
    for (int i = 0; i < 8; i++) {
        // Apply rotations: first pitch (X), then roll (Y)
        point_3d_t rotated = rotate_x(&cube->vertices[i], pitch_rad);
        rotated = rotate_y(&rotated, roll_rad);
        
        // Project to 2D
        cube->projected[i] = project_to_2d(&rotated, cube->center_x, cube->center_y);
    }
}

/**
 * Draw a line between two 2D points
 */
static void draw_line(lv_layer_t* layer, const lv_area_t* coords, 
                      const point_2d_t* p1, const point_2d_t* p2, lv_color_t color)
{
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 2;
    line_dsc.p1.x = coords->x1 + p1->x;
    line_dsc.p1.y = coords->y1 + p1->y;
    line_dsc.p2.x = coords->x1 + p2->x;
    line_dsc.p2.y = coords->y1 + p2->y;
    
    lv_draw_line(layer, &line_dsc);
}

/**
 * 3D cube draw event handler
 */
static void lv_3d_cube_draw_event_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target(e);
    cube_data_t* cube = (cube_data_t*)lv_obj_get_user_data(obj);
    
    if (cube == NULL) return;
    
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t draw_dsc;
    lv_draw_rect_dsc_init(&draw_dsc);
    
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    
    // Draw background
    draw_dsc.bg_color = lv_color_black();
    draw_dsc.bg_opa = LV_OPA_30;
    draw_dsc.radius = 5;
    lv_draw_rect(layer, &draw_dsc, &coords);
    
    // Calculate current projected vertices
    calculate_projected_vertices(cube);
    
    // Draw wireframe edges for the cube
    lv_color_t edge_color = lv_color_white();
    
    // Front face edges
    draw_line(layer, &coords, &cube->projected[0], &cube->projected[1], edge_color);
    draw_line(layer, &coords, &cube->projected[1], &cube->projected[2], edge_color);
    draw_line(layer, &coords, &cube->projected[2], &cube->projected[3], edge_color);
    draw_line(layer, &coords, &cube->projected[3], &cube->projected[0], edge_color);
    
    // Back face edges
    draw_line(layer, &coords, &cube->projected[4], &cube->projected[5], edge_color);
    draw_line(layer, &coords, &cube->projected[5], &cube->projected[6], edge_color);
    draw_line(layer, &coords, &cube->projected[6], &cube->projected[7], edge_color);
    draw_line(layer, &coords, &cube->projected[7], &cube->projected[4], edge_color);
    
    // Connecting edges
    draw_line(layer, &coords, &cube->projected[0], &cube->projected[4], edge_color);
    draw_line(layer, &coords, &cube->projected[1], &cube->projected[5], edge_color);
    draw_line(layer, &coords, &cube->projected[2], &cube->projected[6], edge_color);
    draw_line(layer, &coords, &cube->projected[3], &cube->projected[7], edge_color);
}

/**
 * Create a 3D cube widget
 */
lv_obj_t* lv_3d_cube_create(lv_obj_t* parent, int16_t size)
{
    lv_obj_t* obj = lv_obj_create(parent);
    
    // Allocate cube data
    cube_data_t* cube = (cube_data_t*)lv_malloc(sizeof(cube_data_t));
    if (cube == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for cube data");
        lv_obj_del(obj);
        return NULL;
    }
    
    // Initialize cube data
    cube->cube_size = size;
    cube->roll = 0.0f;
    cube->pitch = 0.0f;
    cube->center_x = size;
    cube->center_y = size;
    
    // Set default colors for faces
    cube->face_colors[0] = lv_color_hex(0xFF0000); // Front: Red
    cube->face_colors[1] = lv_color_hex(0x00FF00); // Back: Green
    cube->face_colors[2] = lv_color_hex(0x0000FF); // Left: Blue
    cube->face_colors[3] = lv_color_hex(0xFFFF00); // Right: Yellow
    cube->face_colors[4] = lv_color_hex(0xFF00FF); // Top: Magenta
    cube->face_colors[5] = lv_color_hex(0x00FFFF); // Bottom: Cyan
    
    // Initialize cube vertices
    init_cube_vertices(cube);
    
    // Set user data
    lv_obj_set_user_data(obj, cube);
    
    // Set object size
    lv_obj_set_size(obj, size * 2, size * 2);
    
    // Add draw event
    lv_obj_add_event_cb(obj, lv_3d_cube_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    
    // Make object non-scrollable
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    
    return obj;
}

/**
 * Update cube orientation from MPU6050 data
 */
void lv_3d_cube_update_orientation(lv_obj_t* cube_obj, const mpu6050_data_t* data)
{
    if (cube_obj == NULL || data == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for cube update");
        return;
    }
    
    cube_data_t* cube = (cube_data_t*)lv_obj_get_user_data(cube_obj);
    if (cube == NULL) {
        ESP_LOGE(TAG, "Invalid cube data");
        return;
    }
    
    // Update rotation angles
    cube->roll = data->roll;
    cube->pitch = data->pitch;
    
    // Trigger redraw
    lv_obj_invalidate(cube_obj);
}

/**
 * Set cube rotation angles manually
 */
void lv_3d_cube_set_rotation(lv_obj_t* cube_obj, float roll, float pitch)
{
    if (cube_obj == NULL) {
        ESP_LOGE(TAG, "Invalid cube object");
        return;
    }
    
    cube_data_t* cube = (cube_data_t*)lv_obj_get_user_data(cube_obj);
    if (cube == NULL) {
        ESP_LOGE(TAG, "Invalid cube data");
        return;
    }
    
    cube->roll = roll;
    cube->pitch = pitch;
    
    // Trigger redraw
    lv_obj_invalidate(cube_obj);
}

/**
 * Set colors for cube faces
 */
void lv_3d_cube_set_colors(lv_obj_t* cube_obj, const lv_color_t colors[6])
{
    if (cube_obj == NULL || colors == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for color setting");
        return;
    }
    
    cube_data_t* cube = (cube_data_t*)lv_obj_get_user_data(cube_obj);
    if (cube == NULL) {
        ESP_LOGE(TAG, "Invalid cube data");
        return;
    }
    
    for (int i = 0; i < 6; i++) {
        cube->face_colors[i] = colors[i];
    }
    
    // Trigger redraw
    lv_obj_invalidate(cube_obj);
}
