/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 * 20240329
 *
 * 
 * Current reference Code for the following products 
 * 1.78 inch AMOLED from DWO LIMITED
 * Part number: DO0180FMST03-QSPI  / DO0180FMST06-QSPI  / DO0180FMST07-QSPI  / DO0180FMST07-QSPI  / DO0180FMST08-QSPI  / DO0180FMST09-QSPI  / DO0180FMST10-QSPI
 * Size: 1.78 inch
 * Resolution: 368x448
 * Signal interface:  QSPI 
  */
  
 /* 
 * For more product information, please visit www.dwo.net.cn
 * DWO LIMITED 
 * AMOLED screen customization.
 * www.dwo.net.cn
 * alan@dwo.net.cn
 * Tel: 86-755-82580810
 * handheld devices, industrial equipment, smart instruments, outdoor displays, smart homes. 
 */


#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lv_demos.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"

static const char *TAG = "example";
// static SemaphoreHandle_t lvgl_mux = NULL;

#define LCD_HOST    SPI2_HOST
#define TOUCH_HOST  I2C_NUM_0

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_21) 
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_48)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_47)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_45)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (-1)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES              368
#define EXAMPLE_LCD_V_RES              448

#define EXAMPLE_USE_TOUCH               0

#if EXAMPLE_USE_TOUCH
#define EXAMPLE_PIN_NUM_TOUCH_SCL         (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_SDA         (GPIO_NUM_46)
#define EXAMPLE_PIN_NUM_TOUCH_RST         (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_TOUCH_INT         (GPIO_NUM_9)

esp_lcd_touch_handle_t tp = NULL;
#endif

#define EXAMPLE_LVGL_BUF_HEIGHT        (EXAMPLE_LCD_V_RES / 4)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
//1.78 inch AMOLED from DWO LIMITED
//Part number: DO0180FMST03-QSPI  / DO0180FMST06-QSPI  / DO0180FMST07-QSPI  / DO0180FMST07-QSPI  / DO0180FMST08-QSPI  / DO0180FMST09-QSPI  / DO0180FMST10-QSPI
// Size: 1.78 inch
// Resolution: 368x448
// Signal interface:  QSPI
//For more product information, please visit www.dwo.net.cn    

    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 10},
    {0x2A, (uint8_t []){0x00,0x00,0x01,0x6F}, 4, 0},  
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xBF}, 4, 0},
    {0x51, (uint8_t []){0x00}, 1, 10},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x51, (uint8_t []){0xFF}, 1, 0},
};

static lv_display_t*  disp_drv;    
volatile bool disp_flush_enabled = true;

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    // lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_drv);
    // if (disp_drv != NULL) {
    //     lv_display_flush_ready(disp_drv);     //因为是DMA 应该加到这里  就看example_notify_lvgl_flush_ready的底层实现了
    // }
    return false;
}


#if EXAMPLE_USE_TOUCH
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)drv->user_data;
    assert(tp);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    /* Read data from touch controller into memory */
    esp_lcd_touch_read_data(tp);
    /* Read data from touch controller */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0) {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "Touch position: %d,%d", tp_x, tp_y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void disp_flush(lv_display_t * disp_drv, const lv_area_t * area, uint8_t * px_map)
{
    if(disp_flush_enabled) {
        esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp_drv);
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    // lv_display_flush_ready(disp_drv); 
}

/**
 * @brief  Safe read from 'elapsed_power_on_time_in_ms'
 */
uint32_t  my_tick_get_cb()
{
    // uint32_t ms = esp_timer_get_time() / 1000;
    // printf("Current time: %u ms\n", ms);
    return esp_timer_get_time() / 1000;
}

void app_main(void)
{
    // static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // contains callback functions
    // Set GPIO11 high  这是屏幕的电源使能
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << 11,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(11, 1);

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA0,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA1,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA2,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA3,
                                                                 EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv);    //disp_drv只是传了一个参数
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 使用 esp_lcd_panel_init 相关接口画一个 10x10 的框
    // 这里只能直接操作底层显存，画一个 10x10 区域为白色
    // uint16_t color = 0x0000; // 16位黑色
    // uint16_t buf[10 * 10];
    // for(int i = 0; i < 100; i++) buf[i] = color;
    // esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 100, 100, buf);

#if EXAMPLE_USE_TOUCH
    ESP_LOGI(TAG, "Initialize I2C bus");
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 600 * 1000,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_HOST, i2c_conf.mode, 0, 0, 0));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    // Attach the TOUCH to the I2C bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_LOGI(TAG, "Initialize touch controller");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp));
#endif

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    lv_tick_set_cb(my_tick_get_cb);

    disp_drv = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    // 绑定 panel_handle 到 LVGL display 的 user_data
    lv_display_set_user_data(disp_drv, panel_handle);
    lv_display_set_flush_cb(disp_drv, disp_flush);


    LV_ATTRIBUTE_MEM_ALIGN
    static uint8_t *buf_1_1 = NULL;
    buf_1_1 = (uint8_t *)heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES/4 * (LCD_BIT_PER_PIXEL / 8), MALLOC_CAP_DMA);
    assert(buf_1_1);
    lv_display_set_buffers(disp_drv, buf_1_1, NULL, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES/4 * (LCD_BIT_PER_PIXEL / 8), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 创建一个按钮并设置标签
    // lv_obj_t *btn = lv_btn_create(lv_scr_act());
    // lv_obj_center(btn);

    // lv_obj_t *label = lv_label_create(btn);
    // lv_label_set_text(label, "button");
    // lv_obj_center(label);

    lv_demo_widgets();

    while(1) {
        lv_timer_periodic_handler();
        vTaskDelay(pdMS_TO_TICKS(5)); // 建议延时 2~10ms
    }
}