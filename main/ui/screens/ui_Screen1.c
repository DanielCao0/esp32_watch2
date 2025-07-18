// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.5.3
// LVGL version: 9.2.2
// Project name: SquareLine_Project

#include "../ui.h"

lv_obj_t * uic_minutes;
lv_obj_t * ui_Screen1 = NULL;
lv_obj_t * ui_time = NULL;
lv_obj_t * ui_hour = NULL;
lv_obj_t * ui_minutes = NULL;
// event funtions

// build funtions

void ui_Screen1_screen_init(void)
{
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_remove_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_time = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(ui_time);
    lv_obj_set_width(ui_time, 302);
    lv_obj_set_height(ui_time, 395);
    lv_obj_set_x(ui_time, 6);
    lv_obj_set_y(ui_time, -3);
    lv_obj_set_align(ui_time, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_time, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_time, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ui_time, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_hour = lv_label_create(ui_time);
    lv_obj_set_width(ui_hour, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_hour, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_hour, LV_ALIGN_CENTER);
    lv_label_set_text(ui_hour, "12");
    lv_obj_set_style_text_font(ui_hour, &ui_font_Font200, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_minutes = lv_label_create(ui_time);
    lv_obj_set_width(ui_minutes, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_minutes, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_minutes, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_minutes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_minutes, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_label_set_text(ui_minutes, "55");
    lv_obj_set_style_text_font(ui_minutes, &ui_font_Font200, LV_PART_MAIN | LV_STATE_DEFAULT);

    uic_minutes = ui_minutes;

}

void ui_Screen1_screen_destroy(void)
{
    if(ui_Screen1) lv_obj_del(ui_Screen1);

    // NULL screen variables
    ui_Screen1 = NULL;
    ui_time = NULL;
    ui_hour = NULL;
    uic_minutes = NULL;
    ui_minutes = NULL;

}
