#ifndef __MENU_SCREEN_H__
#define __MENU_SCREEN_H__   

#include "lvgl.h"

// 原有功能
void home_screen_custom_setup();
// void list_menu_setup();

// 新增的独立界面管理API
lv_obj_t* create_honeycomb_menu_screen(void);  // 创建蜂窝菜单界面
void show_honeycomb_menu(void);                // 显示蜂窝菜单界面
void hide_honeycomb_menu(void);                // 隐藏蜂窝菜单界面
void reset_honeycomb_menu(void);               // 重置菜单到中心位置
void destroy_honeycomb_menu(void);             // 销毁菜单界面

#endif // __MENU_SCREEN_H__