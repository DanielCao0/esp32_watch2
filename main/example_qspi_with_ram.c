/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 * 20240329
 *
 *
 * Current reference Code for the following products
 * 1.78 inch AMOLED from DWO LIMITED
 * Part number: DO0180FMST03-QSPI  / DO0180FMS        // 更新LED
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, led1_r, led1_g, led1_b));
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 1, led2_r, led2_g, led2_b));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        
        vTaskDelay(pdMS_TO_TICKS(50)); // 50ms延时，产生更慢更平滑的颜色过渡PI  / DO0180FMST07-QSPI  / DO0180FMST07-QSPI  / DO0180FMST08-QSPI  / DO0180FMST09-QSPI  / DO0180FMST10-QSPI
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
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

// 禁用旧版I2C驱动的弃用警告
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "driver/i2c.h"
#pragma GCC diagnostic pop

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
#include "led_strip.h"

#include "ui.h"
#include "menu_screen.h"
#include "wifi_connect.h"
#include "clock.h"
#include "lvgl_button.h"
#include "mpu6050.h"
#include "rtc.h"
#include "lvgl_lock.h"

static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;

/**
 * @brief 获取LVGL互斥信号量
 * @param timeout_ms 超时时间（毫秒）
 * @return true - 成功获取，false - 超时失败
 */
bool lvgl_lock(uint32_t timeout_ms)
{
    if (lvgl_mux == NULL) {
        return false;
    }
    return xSemaphoreTake(lvgl_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

/**
 * @brief 释放LVGL互斥信号量
 */
void lvgl_unlock(void)
{
    if (lvgl_mux != NULL) {
        xSemaphoreGive(lvgl_mux);
    }
}

#define LCD_HOST SPI2_HOST
#define TOUCH_HOST I2C_NUM_0

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_PCLK (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_LCD_DATA0 (GPIO_NUM_48)
#define EXAMPLE_PIN_NUM_LCD_DATA1 (GPIO_NUM_47)
#define EXAMPLE_PIN_NUM_LCD_DATA2 (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3 (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_RST (GPIO_NUM_45)
#define EXAMPLE_PIN_NUM_LCD_TE (GPIO_NUM_8)
#define EXAMPLE_PIN_NUM_BK_LIGHT (-1)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 368
#define EXAMPLE_LCD_V_RES 448

#define EXAMPLE_USE_TOUCH 1

#if EXAMPLE_USE_TOUCH
#define EXAMPLE_PIN_NUM_TOUCH_SCL (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_SDA (GPIO_NUM_46)
#define EXAMPLE_PIN_NUM_TOUCH_RST (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_TOUCH_INT (GPIO_NUM_9)

esp_lcd_touch_handle_t tp = NULL;
#endif

#define EXAMPLE_LVGL_BUF_HEIGHT (EXAMPLE_LCD_V_RES / 4)
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2

#define BLINK_GPIO 15

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    // 1.78 inch AMOLED from DWO LIMITED
    // Part number: DO0180FMST03-QSPI  / DO0180FMST06-QSPI  / DO0180FMST07-QSPI  / DO0180FMST07-QSPI  / DO0180FMST08-QSPI  / DO0180FMST09-QSPI  / DO0180FMST10-QSPI
    // Size: 1.78 inch
    // Resolution: 368x448
    // Signal interface:  QSPI
    // For more product information, please visit www.dwo.net.cn

    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},      // TE信号使能，mode 0（V-blank only）
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static lv_display_t *disp_drv;
volatile bool disp_flush_enabled = true;

// TE信号相关
static SemaphoreHandle_t te_sem = NULL;

// TE信号中断服务程序
static void IRAM_ATTR te_gpio_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // 释放信号量，通知刷新任务
    if (te_sem != NULL) {
        xSemaphoreGiveFromISR(te_sem, &xHigherPriorityTaskWoken);
    }
    
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

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
static void example_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t tp_cnt = 0;
    /* Read data from touch controller into memory */
    esp_lcd_touch_read_data(tp);
    /* Read data from touch controller */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "Touch position: %d,%d", tp_x, tp_y);
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    if (disp_flush_enabled)
    {
        esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp_drv);
        
        // // 等待TE信号，防止撕裂（超时时间设为16ms，约60fps）
        // if (te_sem != NULL) {
        //     if (xSemaphoreTake(te_sem, pdMS_TO_TICKS(16)) == pdTRUE) {
        //         // TE信号收到，可以安全刷新
        //     } else {
        //         // 超时，直接刷新（避免卡死）
        //         ESP_LOGW(TAG, "TE timeout, flush anyway");
        //     }
        // }
        
        // 进行字节序转换（小端转大端）
        int pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
        uint16_t *buf = (uint16_t *)px_map;
        int i = 0;
        // 每次处理4个像素
        for (; i <= pixel_count - 4; i += 4)
        {
            buf[i] = (buf[i] >> 8) | (buf[i] << 8);
            buf[i + 1] = (buf[i + 1] >> 8) | (buf[i + 1] << 8);
            buf[i + 2] = (buf[i + 2] >> 8) | (buf[i + 2] << 8);
            buf[i + 3] = (buf[i + 3] >> 8) | (buf[i + 3] << 8);
        }
        // 处理剩余像素
        for (; i < pixel_count; i++)
        {
            buf[i] = (buf[i] >> 8) | (buf[i] << 8);
        }
        esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    }

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    // lv_display_flush_ready(disp_drv);
}

/**
 * @brief  Safe read from 'elapsed_power_on_time_in_ms'
 */
uint32_t my_tick_get_cb()
{
    // uint32_t ms = esp_timer_get_time() / 1000;
    // printf("Current time: %u ms\n", ms);
    return esp_timer_get_time() / 1000;
}

// LED控制任务 - 霓虹色呼吸灯效果
static void led_breathing_task(void *pvParameters)
{
    led_strip_handle_t led_strip = (led_strip_handle_t)pvParameters;
    
    uint8_t brightness = 10;   // 从最暗开始，实现真正的"暗→亮→暗"呼吸循环
    int8_t direction = 1;      // 开始时向亮的方向变化
    uint16_t color_step = 0;   // 颜色过渡步进，0-499 (每个阶段100步，共5个阶段)
    
    // 定义酷炫的霓虹色循环路径 (更丰富的色彩变化)
    uint8_t colors[6][3] = {
        {255, 20, 147},   // [0] 深粉红 (DeepPink) - 妖艳粉红
        {0, 255, 255},    // [1] 青色 (Cyan) - 清爽青蓝
        {138, 43, 226},   // [2] 蓝紫色 (BlueViolet) - 神秘紫罗兰
        {50, 205, 50},    // [3] 酸橙绿 (LimeGreen) - 活力青柠
        {255, 69, 0},     // [4] 橙红色 (OrangeRed) - 热情橙红
        {255, 215, 0}     // [5] 金黄色 (Gold) - 华丽金色
    };
    
    while (1) {
        // 计算呼吸亮度 - 支持完整的暗→亮→暗循环
        brightness += direction * 3;  // 适中的亮度变化速度，保证平滑效果
        if (brightness >= 250) {      // 接近最亮时开始变暗
            brightness = 250;
            direction = -1;           // 转向变暗
        } else if (brightness <= 8) { // 接近最暗时开始变亮
            brightness = 8;
            direction = 1;            // 转向变亮
        }
        
        // 酷炫霓虹色渐变：深粉红 -> 青色 -> 蓝紫色 -> 酸橙绿 -> 橙红色 -> 金黄色...
        color_step = (color_step + 1) % 600; // 600步完成一个完整循环 (每色100步，6种颜色)
        
        // 计算当前处于哪个颜色阶段和阶段内进度
        uint8_t phase = color_step / 100;           // 0-5: 6个颜色过渡阶段
        uint8_t progress = color_step % 100;        // 当前阶段内的进度 (0-99)
        
        // 获取起始颜色和目标颜色
        uint8_t start_color = phase;
        uint8_t end_color = (phase + 1) % 6;        // 现在是6种颜色循环
        
        // 线性插值计算当前RGB值 - 平滑颜色过渡
        uint8_t led1_r = colors[start_color][0] + (colors[end_color][0] - colors[start_color][0]) * progress / 99;
        uint8_t led1_g = colors[start_color][1] + (colors[end_color][1] - colors[start_color][1]) * progress / 99;
        uint8_t led1_b = colors[start_color][2] + (colors[end_color][2] - colors[start_color][2]) * progress / 99;
        
        // LED2可以实现略微不同的色彩效果，增加层次感
        uint16_t led2_color_step = (color_step + 200) % 600; // LED2相位偏移200步，营造不同步效果
        uint8_t led2_phase = led2_color_step / 100;
        uint8_t led2_progress = led2_color_step % 100;
        uint8_t led2_start_color = led2_phase;
        uint8_t led2_end_color = (led2_phase + 1) % 6;
        
        uint8_t led2_r = colors[led2_start_color][0] + (colors[led2_end_color][0] - colors[led2_start_color][0]) * led2_progress / 99;
        uint8_t led2_g = colors[led2_start_color][1] + (colors[led2_end_color][1] - colors[led2_start_color][1]) * led2_progress / 99;
        uint8_t led2_b = colors[led2_start_color][2] + (colors[led2_end_color][2] - colors[led2_start_color][2]) * led2_progress / 99;
        
        // 应用呼吸亮度到两个LED，形成真正的呼吸灯效果
        led1_r = (led1_r * brightness) / 250;  // 除以最大亮度值
        led1_g = (led1_g * brightness) / 250;
        led1_b = (led1_b * brightness) / 250;
        
        led2_r = (led2_r * brightness) / 250;
        led2_g = (led2_g * brightness) / 250;
        led2_b = (led2_b * brightness) / 250;
        
        // 更新两个WS2812B LED灯珠
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, led1_r, led1_g, led1_b));
        ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 1, led2_r, led2_g, led2_b));
        ESP_ERROR_CHECK(led_strip_refresh(led_strip));
        
        vTaskDelay(pdMS_TO_TICKS(60)); // 60ms延时，平滑的呼吸节奏
    }
}

void app_main(void)
{
    // 初始化LVGL互斥信号量
    ESP_LOGI(TAG, "Creating LVGL mutex");
    lvgl_mux = xSemaphoreCreateMutex();
    if (lvgl_mux == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return;
    }

    /// LED strip common configuration
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,                                // The GPIO that connected to the LED strip's data line
        .max_leds = 2,                                               // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,                               // LED strip model, it determines the bit timing
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color component format is G-R-B
        .flags = {
            .invert_out = false, // don't invert the output signal
        }};

    /// RMT backend specific configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,    // different clock source can lead to different power consumption
        .resolution_hz = 10 * 1000 * 1000, // RMT counter clock frequency: 10MHz
        .mem_block_symbols = 64,           // the memory size of each RMT channel, in words (4 bytes)
        .flags = {
            .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
        }};

    /// Create the LED strip object
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));

    // 创建LED呼吸灯任务
    xTaskCreate(led_breathing_task, "led_breathing", 2048, led_strip, 5, NULL);

    // 初始化TE信号
    ESP_LOGI(TAG, "Initialize TE signal");
    
    // 创建TE信号量
    te_sem = xSemaphoreCreateBinary();
    if (te_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create TE semaphore");
    }
    
    // 配置TE引脚为输入，启用上拉
    gpio_config_t te_gpio_config = {
        .pin_bit_mask = (1ULL << EXAMPLE_PIN_NUM_LCD_TE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,  // TE信号上升沿触发
    };
    ESP_ERROR_CHECK(gpio_config(&te_gpio_config));
    
    // 安装GPIO中断服务
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    // 添加TE引脚中断处理程序
    ESP_ERROR_CHECK(gpio_isr_handler_add(EXAMPLE_PIN_NUM_LCD_TE, te_gpio_isr_handler, NULL));

    // static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    // contains callback functions
    // Set GPIO11 high  这是屏幕的电源使能
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << 11,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(11, 1);

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
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
                                                                                &disp_drv); // disp_drv只是传了一个参数
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
        .master.clk_speed = 100 * 1000,
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
    LV_ATTRIBUTE_MEM_ALIGN
    static uint8_t *buf_1_2 = NULL;
    size_t buf_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 10 * (LCD_BIT_PER_PIXEL / 8);
    buf_1_1 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    buf_1_2 = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    assert(buf_1_1);
    assert(buf_1_2);
    lv_display_set_buffers(disp_drv, buf_1_1, buf_1_2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();              /* Create input device connected to Default Display. */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);    /* Touch pad is a pointer-like device. */
    lv_indev_set_read_cb(indev, example_lvgl_touch_cb); /* Set driver function. */
    lv_indev_set_user_data(indev, tp);                  /* 绑定 tp 句柄到 user_data，供回调使用 */

    //list_menu_setup();
    //home_screen_custom_setup();
    ui_init(); // 初始化UI

    wifi_connect_init();   //我放在LVGL的前面 LVGL就会初始化失败，放在后面就可以

    // 初始化时钟系统（15分钟定时同步）
    clock_init();


    // 初始化BOOT按钮
    init_boot_btn();

    // 初始化MPU6050传感器（1秒读取一次）
    // ESP_LOGI(TAG, "Initializing MPU6050 sensor...");
    // esp_err_t mpu_ret = mpu6050_init();
    // if (mpu_ret == ESP_OK) {
    //     // 启动1秒定时读取任务
    //     mpu_ret = mpu6050_start_reading_task_with_interval(1000);  // 1000ms = 1秒
    //     if (mpu_ret == ESP_OK) {
    //         ESP_LOGI(TAG, "MPU6050 sensor initialized and task started (1 second interval)");
    //     } else {
    //         ESP_LOGE(TAG, "Failed to start MPU6050 reading task: %s", esp_err_to_name(mpu_ret));
    //     }
    // } else {
    //     ESP_LOGE(TAG, "Failed to initialize MPU6050 sensor: %s", esp_err_to_name(mpu_ret));
    // }

    while (1)
    {
        // 检查并处理按钮事件
        QueueHandle_t btn_queue = get_button_event_queue();
        if (btn_queue != NULL) {
            button_event_t btn_event;
            // 非阻塞检查按钮事件
            if (xQueueReceive(btn_queue, &btn_event, 0) == pdTRUE) {
                // 获取LVGL锁来安全执行UI操作
                if (lvgl_lock(100)) {
                    handle_button_event(&btn_event);
                    lvgl_unlock();
                } else {
                    ESP_LOGW(TAG, "Failed to get LVGL lock for button event processing");
                }
            }
        }
        
        // 检查并处理时钟事件
        QueueHandle_t clock_queue = get_clock_event_queue();
        if (clock_queue != NULL) {
            clock_event_t clock_event;
            // 非阻塞检查时钟事件
            if (xQueueReceive(clock_queue, &clock_event, 0) == pdTRUE) {
                // 获取LVGL锁来安全执行UI操作
                if (lvgl_lock(100)) {
                    handle_clock_event(&clock_event);
                    lvgl_unlock();
                } else {
                    ESP_LOGW(TAG, "Failed to get LVGL lock for clock event processing");
                }
            }
        }
        
        // 使用较低的刷新频率，与TE信号同步，减少撕裂
        // 获取LVGL互斥信号量
        if (lvgl_lock(20)) {
            lv_timer_handler_run_in_period(16); /* run lv_timer_handler() every 16ms (~60fps) */
            lvgl_unlock();
        } else {
            ESP_LOGW(TAG, "Failed to take LVGL mutex, skipping this cycle");
            vTaskDelay(pdMS_TO_TICKS(1)); // 短暂延时，避免过度占用CPU
        }
    }
}