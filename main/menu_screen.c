#include "menu_screen.h"
#include "lvgl.h"
#include "esp_log.h"
#include "mpu6050_screen.h"
#include "file_browser.h"
#include "music_player.h"
#include "image_viewer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "MENU_SCREEN";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 屏幕分辨率
#define LCD_H_RES              368
#define LCD_V_RES              448

// 蜂窝菜单参数
#define SCREEN_BODY_MAX        37       // 图标数量：1(中心) + 6(第一圈) + 12(第二圈) + 18(第三圈)
#define ICON_SIZE              50       // 基础图标大小
#define HEX_SPACING_1          100       // 中心到第一圈的距离
#define HEX_SPACING_2          100       // 第一圈到第二圈的距离（减小）
#define HEX_SPACING_3          100       // 第二圈到第三圈的距离
#define MAX_SCALE              2.2f     // 最大缩放（中心）- 稍微增大以增强层次感
#define MIN_SCALE              0.15f    // 最小缩放（边缘）- 变得像点一样小
#define SCALE_DISTANCE         220      // 缩放影响距离（稍微减小，让中心区域更突出）

// Apple Watch 应用名称 - 重复有功能的应用以便测试
static const char* app_names[SCREEN_BODY_MAX] = {
    "Clock", "MPU6050", "Files", "Music", "Photos", "TV", "Clock",
    "MPU6050", "Files", "Music", "Photos", "TV", "Clock", "MPU6050",
    "Files", "Music", "Photos", "TV", "Clock", "MPU6050", "Files",
    "Music", "Photos", "TV", "Clock", "MPU6050", "Files", "Music",
    "Photos", "TV", "Clock", "MPU6050", "Files", "Music", "Photos",
    "TV", "Clock"
};

// 简单的图标结构
typedef struct {
    lv_obj_t *cont;
    lv_obj_t *label;
    float hex_x, hex_y;  // 蜂窝坐标
    float scale;         // 当前缩放
    bool is_visible;     // 是否在可见区域
} icon_item_t;

static icon_item_t icons[SCREEN_BODY_MAX];
static float global_offset_x = 0;
static float global_offset_y = 0;

// 惯性滚动相关变量
static float velocity_x = 0;
static float velocity_y = 0;
static lv_timer_t * inertia_timer = NULL;
static const float FRICTION = 0.85f;  // 摩擦系数（更小，摩擦更大，惯性更小）
static const float MIN_VELOCITY = 3.0f;  // 最小速度阈值（更大，更快停止）

// 独立界面管理变量
static lv_obj_t *honeycomb_screen = NULL;
static bool is_honeycomb_initialized = false;

// 前向声明
static void update_icons(void);
static void inertia_timer_cb(lv_timer_t * timer);
static void icon_click_event_cb(lv_event_t * e);

// 恢复到简单有效的圆形分层布局
static void calculate_honeycomb_positions(void) {
    // 六边形坐标系基向量
    const float cos30 = cos(M_PI / 6);  // sqrt(3)/2 ≈ 0.866
    
    int index = 0;
    int max_ring = 3;  // 三圈：0(中心), 1, 2, 3
    
    // 遍历六边形坐标系的格点
    for (int ring = 0; ring <= max_ring && index < SCREEN_BODY_MAX; ring++) {
        if (ring == 0) {
            // 中心点
            icons[index].hex_x = 0;
            icons[index].hex_y = 0;
            index++;
        } else {
            // 根据圈层选择间距
            float ring_spacing;
            switch (ring) {
                case 1: ring_spacing = HEX_SPACING_1; break;  // 中心到第一圈
                case 2: ring_spacing = HEX_SPACING_2; break;  // 第一圈到第二圈
                case 3: ring_spacing = HEX_SPACING_3; break;  // 第二圈到第三圈
                default: ring_spacing = HEX_SPACING_1; break;
            }
            
            // 六边形的六个方向
            for (int direction = 0; direction < 6 && index < SCREEN_BODY_MAX; direction++) {
                // 每个方向上ring个点
                for (int step = 0; step < ring && index < SCREEN_BODY_MAX; step++) {
                    // 六边形坐标系中的坐标
                    float q, r;
                    switch (direction) {
                        case 0: q = ring - step; r = step; break;      // 右上
                        case 1: q = -step; r = ring; break;           // 右
                        case 2: q = -ring; r = ring - step; break;    // 右下
                        case 3: q = -ring + step; r = -step; break;   // 左下
                        case 4: q = step; r = -ring; break;           // 左
                        case 5: q = ring; r = -ring + step; break;    // 左上
                    }
                    
                    // 计算累积间距（每个圈层的实际距离）
                    float cumulative_distance = 0;
                    for (int r = 1; r <= ring; r++) {
                        switch (r) {
                            case 1: cumulative_distance += HEX_SPACING_1; break;
                            case 2: cumulative_distance += HEX_SPACING_2; break;
                            case 3: cumulative_distance += HEX_SPACING_3; break;
                        }
                    }
                    
                    // 转换为笛卡尔坐标，使用累积距离而不是简单的ring*spacing
                    float distance_factor = cumulative_distance / (ring * HEX_SPACING_1);
                    float x = HEX_SPACING_1 * distance_factor * (q + r * 0.5f);
                    float y = HEX_SPACING_1 * distance_factor * (r * cos30);
                    
                    icons[index].hex_x = x;
                    icons[index].hex_y = y;
                    index++;
                }
            }
        }
    }
}

// 计算到屏幕边缘的最小距离
static float distance_to_screen_edge(lv_coord_t x, lv_coord_t y, lv_coord_t icon_size) {
    // 计算图标边界到屏幕边缘的最小距离
    float half_size = icon_size * 0.5f;
    
    float left_dist = x - half_size;           // 到左边缘距离
    float right_dist = LCD_H_RES - (x + half_size);  // 到右边缘距离
    float top_dist = y - half_size;            // 到上边缘距离
    float bottom_dist = LCD_V_RES - (y + half_size);  // 到下边缘距离
    
    // 返回最小距离（如果为负数，说明已经超出边界）
    float min_dist = fminf(fminf(left_dist, right_dist), fminf(top_dist, bottom_dist));
    return min_dist;
}

// 计算基于距离的平滑缩放比例 - 基于到屏幕中心的距离进行基础缩放
static float calculate_scale(float center_distance) {
    if (center_distance <= 0) return MAX_SCALE;
    if (center_distance >= SCALE_DISTANCE) return MIN_SCALE;
    
    // 基于到中心距离的基础缩放（增强中心区域的层次感）
    float normalized = center_distance / SCALE_DISTANCE;
    float base_smooth;
    
    if (normalized < 0.3f) {
        // 前30%距离：几乎不缩放，保持大尺寸
        float local_t = normalized / 0.3f;
        base_smooth = 1.0f - local_t * 0.15f;  // 从1.0缓慢缩到0.85
    } else if (normalized < 0.7f) {
        // 中间40%距离：开始明显缩放
        float local_t = (normalized - 0.3f) / 0.4f;
        base_smooth = 0.85f - local_t * 0.35f;  // 从0.85缩到0.5
    } else {
        // 后30%距离：急剧缩放
        float local_t = (normalized - 0.7f) / 0.3f;
        base_smooth = 0.5f - 0.35f * local_t;  // 从0.5急剧缩到0.15
    }
    
    return MIN_SCALE + (MAX_SCALE - MIN_SCALE) * base_smooth;
}

// 基于到屏幕边缘距离的急剧缩放修正
static float apply_edge_scaling(float base_scale, float edge_distance, lv_coord_t icon_size) {
    // 定义边缘缩放的触发距离（当图标接近屏幕边缘时）
    float edge_threshold = icon_size * 0.3f;  // 当图标边缘距离屏幕边缘小于30%图标大小时开始急剧缩放
    
    if (edge_distance > edge_threshold) {
        // 距离边缘还很远，不进行边缘缩放修正
        return base_scale;
    }
    
    if (edge_distance <= 0) {
        // 已经触碰或超出边缘，缩放到最小
        return MIN_SCALE;
    }
    
    // 在边缘阈值内进行急剧缩放
    float edge_factor = edge_distance / edge_threshold;  // 0.0 到 1.0
    float steep_curve = powf(edge_factor, 3.0f);  // 三次方曲线，急剧下降
    
    // 混合基础缩放和边缘缩放
    float edge_scale = MIN_SCALE + (base_scale - MIN_SCALE) * steep_curve;
    
    return edge_scale;
}

// 更新所有图标 - 简化版本，去掉复杂的边缘渐隐
static void update_icons(void) {
    lv_coord_t center_x = LCD_H_RES / 2;
    lv_coord_t center_y = LCD_V_RES / 2;
    
    for (int i = 0; i < SCREEN_BODY_MAX; i++) {
        if (icons[i].cont == NULL) continue;
        
        // 计算屏幕位置
        lv_coord_t screen_x = center_x + (lv_coord_t)(icons[i].hex_x + global_offset_x);
        lv_coord_t screen_y = center_y + (lv_coord_t)(icons[i].hex_y + global_offset_y);
        
        // 计算距离中心的距离
        float dx = screen_x - center_x;
        float dy = screen_y - center_y;
        float center_distance = sqrtf(dx * dx + dy * dy);
        
        // 基于中心距离计算基础缩放
        float base_scale = calculate_scale(center_distance);
        
        // 计算到屏幕边缘的距离
        float edge_distance = distance_to_screen_edge(screen_x, screen_y, ICON_SIZE * base_scale);
        
        // 应用边缘缩放修正
        icons[i].scale = apply_edge_scaling(base_scale, edge_distance, ICON_SIZE);
        
        // 计算缩放后的尺寸
        lv_coord_t scaled_size = (lv_coord_t)(ICON_SIZE * icons[i].scale);
        if (scaled_size < 15) scaled_size = 15;  // 更小的最小尺寸
        if (scaled_size > 100) scaled_size = 100; // 更大的最大尺寸限制
        
        // 检查是否在可见区域内（包含一些边距）
        lv_coord_t margin = scaled_size;
        icons[i].is_visible = (screen_x + margin >= 0 && screen_x - margin <= LCD_H_RES &&
                              screen_y + margin >= 0 && screen_y - margin <= LCD_V_RES);
        
        if (icons[i].is_visible) {
            // 更新位置和大小
            lv_obj_set_size(icons[i].cont, scaled_size, scaled_size);
            lv_obj_set_pos(icons[i].cont, screen_x - scaled_size/2, screen_y - scaled_size/2);
            
            // 显示对象
            lv_obj_clear_flag(icons[i].cont, LV_OBJ_FLAG_HIDDEN);
            
            // 根据缩放调整字体大小
            if (icons[i].scale > 1.5f) {
                lv_obj_set_style_text_font(icons[i].label, &lv_font_montserrat_14, LV_PART_MAIN);
            } else if (icons[i].scale > 1.2f) {
                lv_obj_set_style_text_font(icons[i].label, &lv_font_montserrat_12, LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_font(icons[i].label, &lv_font_montserrat_12, LV_PART_MAIN);
            }
        } else {
            // 隐藏不可见的图标以提高性能
            lv_obj_add_flag(icons[i].cont, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 惯性动画定时器回调
static void inertia_timer_cb(lv_timer_t * timer) {
    // 应用摩擦力
    velocity_x *= FRICTION;
    velocity_y *= FRICTION;
    
    // 检查是否需要停止动画
    if (fabsf(velocity_x) < MIN_VELOCITY && fabsf(velocity_y) < MIN_VELOCITY) {
        if (inertia_timer) {
            lv_timer_del(inertia_timer);
            inertia_timer = NULL;
        }
        return;
    }
    
    // 更新位置
    global_offset_x += velocity_x;
    global_offset_y += velocity_y;
    
    // 更新图标
    update_icons();
}

// 图标点击事件处理
static void icon_click_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t * target = lv_event_get_target(e);
        
        // 找到被点击的图标索引
        int clicked_index = -1;
        for (int i = 0; i < SCREEN_BODY_MAX; i++) {
            if (icons[i].cont == target) {
                clicked_index = i;
                break;
            }
        }
        
        if (clicked_index >= 0) {
            const char* app_name = app_names[clicked_index];
            ESP_LOGI(TAG, "Clicked app: %s (index: %d)", app_name, clicked_index);
            
            // 处理特定应用的点击
            if (strcmp(app_name, "MPU6050") == 0) {
                // 声明来自main.c的函数
                extern lv_obj_t* get_mpu6050_3d_screen(void);
                
                // 切换到MPU6050 3D界面
                lv_obj_t* mpu6050_3d_screen = get_mpu6050_3d_screen();
                if (mpu6050_3d_screen != NULL) {
                    lv_screen_load(mpu6050_3d_screen);
                    ESP_LOGI(TAG, "Switched to MPU6050 3D screen");
                } else {
                    ESP_LOGW(TAG, "MPU6050 3D screen not initialized");
                }
            } else if (strcmp(app_name, "Files") == 0) {
                // 切换到文件浏览器
                lv_obj_t* file_browser_screen = get_file_browser_screen();
                if (file_browser_screen != NULL) {
                    //lv_screen_load(file_browser_screen);
                    lv_scr_load_anim(file_browser_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);

                    // 强制刷新文件列表
                    extern esp_err_t file_browser_refresh(const char* path);
                    file_browser_refresh("/sdcard");
                    ESP_LOGI(TAG, "Switched to File Browser screen");
                } else {
                    ESP_LOGW(TAG, "File Browser screen not initialized");
                }
            } else if (strcmp(app_name, "Music") == 0) {
                // 切换到音乐播放器
                lv_obj_t* music_player_screen = get_music_player_screen();
                if (music_player_screen == NULL) {
                    // 如果音乐播放器屏幕不存在，创建它
                    music_player_screen = music_player_create();
                }
                if (music_player_screen != NULL) {
                    lv_scr_load_anim(music_player_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
                    ESP_LOGI(TAG, "Switched to Music Player screen");
                } else {
                    ESP_LOGE(TAG, "Music Player screen creation failed");
                }
            } else if (strcmp(app_name, "Photos") == 0) {
                // 切换到图片浏览器
                lv_obj_t* image_viewer_screen = get_image_viewer_screen();
                if (image_viewer_screen == NULL) {
                    // 如果图片浏览器屏幕不存在，创建它
                    image_viewer_screen = image_viewer_create();
                }
                if (image_viewer_screen != NULL) {
                    lv_scr_load_anim(image_viewer_screen, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
                    ESP_LOGI(TAG, "Switched to Image Viewer screen");
                } else {
                    ESP_LOGE(TAG, "Image Viewer screen creation failed");
                }
            } else if (strcmp(app_name, "Clock") == 0) {
                // 切换回主界面（时钟）
                lv_screen_load(lv_screen_active());  // 或者指向主时钟屏幕
                ESP_LOGI(TAG, "Switched to Clock");
            } else if (strcmp(app_name, "TV") == 0) {
                ESP_LOGI(TAG, "TV app clicked, attempting to load image...");
                
                lv_obj_t * img;
                img = lv_image_create(lv_screen_active());
                if (img == NULL) {
                    ESP_LOGE(TAG, "Failed to create image object");
                    return;
                }
                ESP_LOGI(TAG, "Image object created successfully");
                
                const char* image_path = "A:/sdcard/image/pexels1.png";
                ESP_LOGI(TAG, "Attempting to load image from: %s", image_path);
                
                // 设置图片源
                lv_image_set_src(img, image_path);
                
                // 检查图片是否加载成功
                const void* src = lv_image_get_src(img);
                if (src == NULL) {
                    ESP_LOGE(TAG, "Image source is NULL, failed to load: %s", image_path);
                    ESP_LOGE(TAG, "Possible reasons:");
                    ESP_LOGE(TAG, "1. File doesn't exist at the specified path");
                    ESP_LOGE(TAG, "2. File system not mounted correctly");
                    ESP_LOGE(TAG, "3. File format not supported");
                    ESP_LOGE(TAG, "4. A: drive not configured in LVGL");
                } else {
                    ESP_LOGI(TAG, "Image loaded successfully from: %s", image_path);
                }
                
                lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
                lv_obj_move_foreground(img);
                ESP_LOGI(TAG, "Image positioned at center of screen");
            }
            // 可以在这里添加其他应用的处理
        }
    }
}

// 拖动事件处理 - 带惯性效果
static void drag_event_cb(lv_event_t * e) {
    static lv_point_t last_point = {0, 0};
    static lv_point_t prev_point = {0, 0};
    static bool is_dragging = false;
    static bool first_press = true;
    static uint32_t last_time = 0;
    
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t * indev = lv_indev_get_act();
    uint32_t current_time = lv_tick_get();
    
    if (code == LV_EVENT_PRESSED) {
        // 停止惯性动画
        if (inertia_timer) {
            lv_timer_del(inertia_timer);
            inertia_timer = NULL;
        }
        
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        last_point = point;
        prev_point = point;
        is_dragging = true;
        first_press = true;
        last_time = current_time;
        velocity_x = 0;
        velocity_y = 0;
        
    } else if (code == LV_EVENT_PRESSING) {
        if (is_dragging) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            
            // 第一次按压时跳过，避免初始跳跃
            if (first_press) {
                last_point = point;
                prev_point = point;
                first_press = false;
                last_time = current_time;
                return;
            }
            
            // 计算拖动距离
            float dx = point.x - last_point.x;
            float dy = point.y - last_point.y;
            
            // 只有在移动距离足够大时才更新，避免微小抖动
            if (fabsf(dx) > 0.5f || fabsf(dy) > 0.5f) {
                // 更新全局偏移
                global_offset_x += dx;
                global_offset_y += dy;
                
                // 计算速度（用于惯性）
                uint32_t dt = current_time - last_time;
                if (dt > 0) {
                    velocity_x = dx * 1000.0f / dt;  // 像素/秒
                    velocity_y = dy * 1000.0f / dt;
                    
                    // 限制最大速度（减小以获得更温和的惯性）
                    const float MAX_VELOCITY = 800.0f;
                    if (velocity_x > MAX_VELOCITY) velocity_x = MAX_VELOCITY;
                    if (velocity_x < -MAX_VELOCITY) velocity_x = -MAX_VELOCITY;
                    if (velocity_y > MAX_VELOCITY) velocity_y = MAX_VELOCITY;
                    if (velocity_y < -MAX_VELOCITY) velocity_y = -MAX_VELOCITY;
                }
                
                // 更新图标
                update_icons();
                
                prev_point = last_point;
                last_point = point;
                last_time = current_time;
            }
        }
        
    } else if (code == LV_EVENT_RELEASED) {
        is_dragging = false;
        first_press = true;
        
        // 启动惯性动画（如果速度足够大）
        if (fabsf(velocity_x) >= MIN_VELOCITY || fabsf(velocity_y) >= MIN_VELOCITY) {
            // 转换为每帧的速度（假设60fps）
            velocity_x /= 60.0f;
            velocity_y /= 60.0f;
            
            if (inertia_timer == NULL) {
                inertia_timer = lv_timer_create(inertia_timer_cb, 16, NULL);  // ~60fps
            }
        }
    }
}

// LVGL9 原生滚动事件处理
static void scroll_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_SCROLL) {
        lv_obj_t * target = lv_event_get_target(e);
        lv_point_t scroll_offset;
        lv_obj_get_scroll_end(target, &scroll_offset);
        
        // 更新全局偏移（注意LVGL的滚动方向与我们的偏移方向相反）
        global_offset_x = -scroll_offset.x;
        global_offset_y = -scroll_offset.y;
        
        // 更新图标
        update_icons();
    }
}

// 创建蜂窝菜单界面
lv_obj_t* create_honeycomb_menu_screen(void) {
    // 如果已经创建过，直接返回
    if (honeycomb_screen != NULL) {
        return honeycomb_screen;
    }
    
    // 创建新的屏幕对象
    honeycomb_screen = lv_obj_create(NULL);
    
    // 设置黑色背景
    lv_obj_set_style_bg_color(honeycomb_screen, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 禁用LVGL原生滚动，使用自定义拖动
    lv_obj_clear_flag(honeycomb_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(honeycomb_screen, LV_SCROLLBAR_MODE_OFF);
    
    // 计算蜂窝坐标
    calculate_honeycomb_positions();
    
    // Apple Watch 风格颜色 - 扩展色彩
    lv_color_t colors[] = {
        lv_color_hex(0xFF3B30), // 红色
        lv_color_hex(0xFF9500), // 橙色  
        lv_color_hex(0xFFCC02), // 黄色
        lv_color_hex(0x34C759), // 绿色
        lv_color_hex(0x007AFF), // 蓝色
        lv_color_hex(0x5856D6), // 紫色
        lv_color_hex(0xFF2D92), // 粉色
        lv_color_hex(0x8E8E93), // 灰色
        lv_color_hex(0x00C7BE), // 青色
        lv_color_hex(0x30D158), // 亮绿色
        lv_color_hex(0x40C8E0), // 天蓝色
        lv_color_hex(0x5E5CE6), // 蓝紫色
        lv_color_hex(0xAF52DE), // 紫色渐变
        lv_color_hex(0xFF6482), // 浅粉色
        lv_color_hex(0xFF8500), // 深橙色
        lv_color_hex(0x32ADE6), // 深蓝色
    };
    
    // 创建图标
    for (int i = 0; i < SCREEN_BODY_MAX; i++) {
        // 创建容器
        icons[i].cont = lv_obj_create(honeycomb_screen);
        lv_obj_set_size(icons[i].cont, ICON_SIZE, ICON_SIZE);
        lv_obj_set_style_radius(icons[i].cont, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(icons[i].cont, 2, LV_PART_MAIN);
        
        // 设置颜色和视觉效果
        lv_color_t color = colors[i % (sizeof(colors) / sizeof(colors[0]))];
        lv_obj_set_style_bg_color(icons[i].cont, color, LV_PART_MAIN);
        
        // 移除边框以获得更简洁的外观
        lv_obj_set_style_border_width(icons[i].cont, 0, LV_PART_MAIN);
        
        // 禁用图标自身滚动，但保持可点击
        lv_obj_clear_flag(icons[i].cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // 为每个图标添加拖动事件
        lv_obj_add_event_cb(icons[i].cont, drag_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(icons[i].cont, drag_event_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(icons[i].cont, drag_event_cb, LV_EVENT_RELEASED, NULL);
        
        // 添加点击事件
        lv_obj_add_event_cb(icons[i].cont, icon_click_event_cb, LV_EVENT_CLICKED, NULL);
        
        // 创建标签
        icons[i].label = lv_label_create(icons[i].cont);
        lv_label_set_text(icons[i].label, app_names[i]);
        lv_obj_set_style_text_color(icons[i].label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(icons[i].label, &lv_font_montserrat_12, LV_PART_MAIN);
        
        lv_obj_center(icons[i].label);
        
        // 设置初始缩放和可见性
        icons[i].scale = 1.0f;
        icons[i].is_visible = true;
    }
    
    // 添加拖动事件到主容器
    lv_obj_add_event_cb(honeycomb_screen, drag_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(honeycomb_screen, drag_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(honeycomb_screen, drag_event_cb, LV_EVENT_RELEASED, NULL);
    
    // 初始化位置
    global_offset_x = 0;
    global_offset_y = 0;
    is_honeycomb_initialized = true;
    
    ESP_LOGI(TAG, "Apple Watch Honeycomb Menu screen created with %d icons", SCREEN_BODY_MAX);
    
    return honeycomb_screen;
}

// 显示蜂窝菜单界面
void show_honeycomb_menu(void) {
    if (honeycomb_screen == NULL) {
        create_honeycomb_menu_screen();
    }
    
    // 切换到蜂窝菜单界面
    lv_screen_load(honeycomb_screen);
    
    // 刷新图标位置
    if (is_honeycomb_initialized) {
        update_icons();
    }
    
    ESP_LOGI(TAG, "Switched to Honeycomb Menu");
}

// 隐藏蜂窝菜单界面（切换到其他界面）
void hide_honeycomb_menu(void) {
    // 停止惯性动画
    if (inertia_timer) {
        lv_timer_del(inertia_timer);
        inertia_timer = NULL;
    }
    
    ESP_LOGI(TAG, "Hiding Honeycomb Menu");
}

// 重置蜂窝菜单到中心位置
void reset_honeycomb_menu(void) {
    if (!is_honeycomb_initialized) return;
    
    // 停止惯性动画
    if (inertia_timer) {
        lv_timer_del(inertia_timer);
        inertia_timer = NULL;
    }
    
    // 重置偏移
    global_offset_x = 0;
    global_offset_y = 0;
    velocity_x = 0;
    velocity_y = 0;
    
    // 更新图标位置
    update_icons();
    
    ESP_LOGI(TAG, "Honeycomb Menu reset to center");
}

// 销毁蜂窝菜单界面（释放内存）
void destroy_honeycomb_menu(void) {
    if (honeycomb_screen != NULL) {
        // 停止惯性动画
        if (inertia_timer) {
            lv_timer_del(inertia_timer);
            inertia_timer = NULL;
        }
        
        // 删除屏幕对象
        lv_obj_del(honeycomb_screen);
        honeycomb_screen = NULL;
        is_honeycomb_initialized = false;
        
        ESP_LOGI(TAG, "Honeycomb Menu destroyed");
    }
}

// 主设置函数 - 修改为兼容性函数
void home_screen_custom_setup() {
    show_honeycomb_menu();
}
