#include "menu_screen.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>

// 屏幕分辨率
#define LCD_H_RES              368
#define LCD_V_RES              448

// 列表菜单项高度和间距
#define LIST_ITEM_HEIGHT       80
#define LIST_ITEM_PADDING      20
#define ICON_SIZE              60

// 菜单项数量
#define MENU_ITEMS_COUNT       20

// 菜单项名称
static const char* menu_items[] = {
    "Settings", "Messages", "Phone", "Mail", "Camera", "Health",
    "Wallet", "Calculator", "Reminders", "Notes", "Stocks", "News",
    "Podcasts", "Find My", "Siri", "Timer", "Stopwatch", "Alarm",
    "World Clock", "App Store"
};

// 菜单项图标（用简单符号表示）
static const char* menu_icons[] = {
    "*", "!", "@", "&", "#", "+",
    "$", "=", "-", "~", "^", "%",
    ")", "?", "<", "0", "8", "O",
    "W", "A"
};

// 菜单项颜色（十六进制值）
static uint32_t menu_color_values[] = {
    0x8E8E93, // 灰色 - 设置
    0x34C759, // 绿色 - 消息
    0x34C759, // 绿色 - 电话
    0x007AFF, // 蓝色 - 邮件
    0x8E8E93, // 灰色 - 相机
    0xFF3B30, // 红色 - 健康
    0x000000, // 黑色 - 钱包
    0xFF9500, // 橙色 - 计算器
    0xFF9500, // 橙色 - 提醒事项
    0xFFCC02, // 黄色 - 备忘录
    0x000000, // 黑色 - 股市
    0xFF3B30, // 红色 - 新闻
    0x5856D6, // 紫色 - 播客
    0x34C759, // 绿色 - 查找
    0x000000, // 黑色 - Siri
    0x8E8E93, // 灰色 - 定时器
    0xFF9500, // 橙色 - 秒表
    0x000000, // 黑色 - 闹钟
    0x000000, // 黑色 - 世界时钟
    0x007AFF  // 蓝色 - 应用商店
};

// 存储滚动容器和菜单项的引用，用于动态缩放
static lv_obj_t * g_scroll_cont = NULL;
static lv_obj_t * g_menu_items[MENU_ITEMS_COUNT];

// 动态调整菜单项大小的函数
static void update_item_scales(void) {
    if (g_scroll_cont == NULL) return;
    
    lv_coord_t scroll_top = lv_obj_get_scroll_top(g_scroll_cont);
    lv_coord_t cont_height = lv_obj_get_height(g_scroll_cont);
    
    for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
        if (g_menu_items[i] == NULL) continue;
        
        // 计算菜单项在滚动容器中的位置
        lv_coord_t item_y = i * (LIST_ITEM_HEIGHT + LIST_ITEM_PADDING);
        lv_coord_t item_relative_y = item_y - scroll_top;
        
        // 计算缩放比例
        float scale = 1.0f;
        
        // 如果项目在屏幕底部边缘（快要出现）
        if (item_relative_y > cont_height - LIST_ITEM_HEIGHT) {
            // 计算距离底部的距离
            float distance_from_bottom = cont_height - item_relative_y;
            scale = 0.5f + (distance_from_bottom / LIST_ITEM_HEIGHT) * 0.5f;
            scale = LV_MAX(0.5f, LV_MIN(1.0f, scale));
        }
        // 如果项目在屏幕顶部边缘（快要消失）
        else if (item_relative_y < 0) {
            float distance_from_top = item_relative_y + LIST_ITEM_HEIGHT;
            scale = 0.5f + (distance_from_top / LIST_ITEM_HEIGHT) * 0.5f;
            scale = LV_MAX(0.5f, LV_MIN(1.0f, scale));
        }
        
        // 应用缩放
        lv_coord_t scaled_height = (lv_coord_t)(LIST_ITEM_HEIGHT * scale);
        lv_coord_t scaled_width = (lv_coord_t)((LCD_H_RES - 60) * scale);
        
        lv_obj_set_size(g_menu_items[i], scaled_width, scaled_height);
        
        // 居中对齐菜单项
        lv_coord_t container_width = lv_obj_get_width(g_scroll_cont);
        lv_coord_t item_x = (container_width - scaled_width) / 2;
        lv_obj_set_x(g_menu_items[i], item_x);
        
        // 调整图标大小
        lv_obj_t * icon_cont = lv_obj_get_child(g_menu_items[i], 0);
        if (icon_cont != NULL) {
            lv_coord_t scaled_icon_size = (lv_coord_t)(ICON_SIZE * scale);
            lv_obj_set_size(icon_cont, scaled_icon_size, scaled_icon_size);
        }
    }
}

// 滚动事件回调
static void scroll_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_SCROLL) {
        update_item_scales();
    }
}

// 列表项点击回调
static void list_item_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED) {
        // 获取用户数据（菜单项索引）
        uint32_t * item_index = (uint32_t*)lv_event_get_user_data(e);
        if(item_index != NULL) {
            printf("Clicked menu item: %s\n", menu_items[*item_index]);
            
            // 这里可以添加具体的菜单项处理逻辑
            // 比如跳转到对应的界面或执行相应的功能
        }
    }
}

// 创建列表菜单界面
void list_menu_setup() {
    lv_obj_t *main_cont = lv_screen_active();
    
    // 清除之前的内容
    lv_obj_clean(main_cont);
    
    // 设置黑色背景
    lv_obj_set_style_bg_color(main_cont, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(main_cont, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 创建标题栏
    lv_obj_t * title_label = lv_label_create(main_cont);
    lv_label_set_text(title_label, "Menu");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // 创建滚动容器
    lv_obj_t * scroll_cont = lv_obj_create(main_cont);
    g_scroll_cont = scroll_cont;  // 保存引用用于动态缩放
    lv_obj_set_size(scroll_cont, LCD_H_RES - 40, LCD_V_RES - 80);
    lv_obj_align(scroll_cont, LV_ALIGN_CENTER, 0, 20);
    
    // 设置滚动容器样式
    lv_obj_set_style_bg_color(scroll_cont, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 移除滚动条
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);
    
    // 启用垂直滚动
    lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
    
    // 启用惯性滚动和弹性滚动效果
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    
    // 添加滚动事件监听器
    lv_obj_add_event_cb(scroll_cont, scroll_event_cb, LV_EVENT_SCROLL, NULL);
    
    // 创建菜单项
    for(int i = 0; i < MENU_ITEMS_COUNT && i < (sizeof(menu_items)/sizeof(menu_items[0])); i++) {
        // 创建菜单项容器
        lv_obj_t * item_cont = lv_obj_create(scroll_cont);
        g_menu_items[i] = item_cont;  // 保存引用用于动态缩放
        lv_obj_set_size(item_cont, LCD_H_RES - 60, LIST_ITEM_HEIGHT);
        
        // 居中对齐菜单项
        lv_coord_t container_width = LCD_H_RES - 40;  // 滚动容器的宽度
        lv_coord_t item_x = (container_width - (LCD_H_RES - 60)) / 2;
        lv_obj_set_pos(item_cont, item_x, i * (LIST_ITEM_HEIGHT + LIST_ITEM_PADDING));

        // 设置菜单项样式
        lv_obj_set_style_radius(item_cont, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(item_cont, lv_color_hex(0x1C1C1E), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(item_cont, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(item_cont, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(item_cont, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(item_cont, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(item_cont, LIST_ITEM_PADDING, LV_PART_MAIN | LV_STATE_DEFAULT);

        // 点击效果
        lv_obj_set_style_bg_color(item_cont, lv_color_hex(0x2C2C2E), LV_PART_MAIN | LV_STATE_PRESSED);

        // 清除默认的滚动功能，让父容器处理滚动
        lv_obj_clear_flag(item_cont, LV_OBJ_FLAG_SCROLLABLE);

        // 创建图标容器
        lv_obj_t * icon_cont = lv_obj_create(item_cont);
        lv_obj_set_size(icon_cont, ICON_SIZE, ICON_SIZE);
        lv_obj_align(icon_cont, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_radius(icon_cont, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(menu_color_values[i % (sizeof(menu_color_values)/sizeof(menu_color_values[0]))]), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(icon_cont, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_clear_flag(icon_cont, LV_OBJ_FLAG_SCROLLABLE);

        // 创建图标标签
        lv_obj_t * icon_label = lv_label_create(icon_cont);
        lv_label_set_text(icon_label, menu_icons[i % (sizeof(menu_icons)/sizeof(menu_icons[0]))]);
        lv_obj_set_style_text_color(icon_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(icon_label);

        // 创建菜单项标签
        lv_obj_t * item_label = lv_label_create(item_cont);
        lv_label_set_text(item_label, menu_items[i]);
        lv_obj_set_style_text_color(item_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(item_label, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align_to(item_label, icon_cont, LV_ALIGN_OUT_RIGHT_MID, 15, 0);

        // 分配索引内存并添加点击事件
        static uint32_t item_indices[MENU_ITEMS_COUNT];
        item_indices[i] = i;
        lv_obj_add_event_cb(item_cont, list_item_event_cb, LV_EVENT_CLICKED, &item_indices[i]);

        // 启用点击
        lv_obj_add_flag(item_cont, LV_OBJ_FLAG_CLICKABLE);
    }

    // 设置滚动容器的内容高度
    lv_coord_t content_height = MENU_ITEMS_COUNT * (LIST_ITEM_HEIGHT + LIST_ITEM_PADDING) + LIST_ITEM_PADDING;
    lv_obj_set_height(scroll_cont, LV_MIN(content_height, LCD_V_RES - 80));
    
    // 更新布局
    lv_obj_update_layout(main_cont);
    
    // 初始化缩放效果
    update_item_scales();
    
    printf("List menu created with %d menu items\n", MENU_ITEMS_COUNT);
}
