#include "lvgl_button.h"
#include "menu_screen.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BOOT_BTN";

// 定义BOOT按键GPIO
#define BOOT_BTN_GPIO 0

// 按钮配置常量
#define BTN_DEBOUNCE_TIME_MS    50      // 防抖时间（毫秒）
#define BTN_LONG_PRESS_TIME_MS  1000    // 长按时间（毫秒）
#define BTN_POLL_INTERVAL_MS    20      // 检测间隔（毫秒）
#define DOUBLE_CLICK_TIME_MS    500     // 双击检测时间窗口（毫秒）
#define BTN_TASK_STACK_SIZE     2048    // 按钮任务栈大小
#define BTN_TASK_PRIORITY       5       // 按钮任务优先级

// 按钮状态变量
static volatile button_state_t btn_state = BTN_STATE_IDLE;
static volatile uint32_t btn_press_start_time = 0;
static volatile bool btn_long_press_handled = false;

// 按钮事件回调
static volatile button_event_cb_t short_press_cb = NULL;
static volatile button_event_cb_t long_press_cb = NULL;
static volatile button_event_cb_t double_click_cb = NULL;

// 双击检测相关
static volatile uint32_t last_click_time = 0;
static volatile bool waiting_for_double_click = false;

// 任务句柄（用于调试和管理）
static TaskHandle_t btn_task_handle = NULL;

// 获取系统时间（毫秒）
static inline uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// 短按事件处理
static void handle_short_press(void)
{
    ESP_LOGI(TAG, "Short press detected");
    
    uint32_t current_time = get_time_ms();
    
    if (waiting_for_double_click && (current_time - last_click_time) < DOUBLE_CLICK_TIME_MS) {
        // 双击检测成功
        waiting_for_double_click = false;
        ESP_LOGI(TAG, "Double click confirmed (interval: %lu ms)", current_time - last_click_time);
        if (double_click_cb != NULL) {
            double_click_cb();
        } else {
            ESP_LOGW(TAG, "Double click callback not registered");
        }
    } else {
        // 开始等待双击或执行单击
        waiting_for_double_click = true;
        last_click_time = current_time;
        ESP_LOGD(TAG, "Waiting for potential double click...");
    }
}

// 长按事件处理
static void handle_long_press(void)
{
    ESP_LOGI(TAG, "Long press confirmed (held for %lu ms)", get_time_ms() - btn_press_start_time);
    if (long_press_cb != NULL) {
        long_press_cb();
    } else {
        ESP_LOGW(TAG, "Long press callback not registered");
    }
}

// 处理等待中的单击事件
static void handle_pending_single_click(void)
{
    if (waiting_for_double_click) {
        uint32_t current_time = get_time_ms();
        if ((current_time - last_click_time) >= DOUBLE_CLICK_TIME_MS) {
            waiting_for_double_click = false;
            ESP_LOGI(TAG, "Single click confirmed (no double click detected)");
            if (short_press_cb != NULL) {
                short_press_cb();
            } else {
                ESP_LOGW(TAG, "Short press callback not registered");
            }
        }
    }
}

// 按钮检测任务 - 优化版状态机
static void boot_btn_task(void *pvParameter)
{
    int current_level = 1;      // 当前GPIO电平
    int stable_level = 1;       // 稳定的GPIO电平
    uint32_t level_change_time = 0;
    uint32_t current_time;
    
    ESP_LOGI(TAG, "Boot button detection task started (GPIO%d)", BOOT_BTN_GPIO);
    ESP_LOGI(TAG, "Task config: debounce=%dms, long_press=%dms, poll=%dms", 
             BTN_DEBOUNCE_TIME_MS, BTN_LONG_PRESS_TIME_MS, BTN_POLL_INTERVAL_MS);
    
    while (1) {
        current_level = gpio_get_level(BOOT_BTN_GPIO);
        current_time = get_time_ms();
        
        // 防抖处理 - 检测电平变化并确认稳定性
        if (current_level != stable_level) {
            if (level_change_time == 0) {
                level_change_time = current_time;
            } else if ((current_time - level_change_time) >= BTN_DEBOUNCE_TIME_MS) {
                stable_level = current_level;
                level_change_time = 0;
                ESP_LOGD(TAG, "Level stabilized: %d", stable_level);
            }
        } else {
            level_change_time = 0;
        }
        
        // 状态机处理
        switch (btn_state) {
            case BTN_STATE_IDLE:
                if (stable_level == 0) { // 按钮按下（低电平）
                    btn_state = BTN_STATE_PRESSED;
                    btn_press_start_time = current_time;
                    btn_long_press_handled = false;
                    ESP_LOGD(TAG, "State: IDLE -> PRESSED");
                }
                handle_pending_single_click(); // 检查等待中的单击
                break;
                
            case BTN_STATE_PRESSED:
                if (stable_level == 1) { // 按钮释放
                    btn_state = BTN_STATE_RELEASED;
                    ESP_LOGD(TAG, "State: PRESSED -> RELEASED (duration: %lu ms)", 
                            current_time - btn_press_start_time);
                } else if (!btn_long_press_handled && 
                          (current_time - btn_press_start_time) >= BTN_LONG_PRESS_TIME_MS) {
                    // 长按检测
                    btn_state = BTN_STATE_HELD;
                    btn_long_press_handled = true;
                    ESP_LOGD(TAG, "State: PRESSED -> HELD");
                    handle_long_press();
                }
                break;
                
            case BTN_STATE_HELD:
                if (stable_level == 1) { // 长按后释放
                    btn_state = BTN_STATE_IDLE;
                    ESP_LOGD(TAG, "State: HELD -> IDLE (total duration: %lu ms)", 
                            current_time - btn_press_start_time);
                }
                break;
                
            case BTN_STATE_RELEASED:
                if (!btn_long_press_handled) {
                    handle_short_press();
                }
                btn_state = BTN_STATE_IDLE;
                ESP_LOGD(TAG, "State: RELEASED -> IDLE");
                break;
                
            default:
                ESP_LOGE(TAG, "Invalid button state: %d", btn_state);
                btn_state = BTN_STATE_IDLE;
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_INTERVAL_MS));
    }
}

// 注册按钮事件回调函数
void button_register_short_press_cb(button_event_cb_t callback)
{
    if (callback != NULL) {
        short_press_cb = callback;
        ESP_LOGI(TAG, "Short press callback registered successfully");
    } else {
        ESP_LOGW(TAG, "Attempted to register NULL short press callback");
    }
}

void button_register_long_press_cb(button_event_cb_t callback)
{
    if (callback != NULL) {
        long_press_cb = callback;
        ESP_LOGI(TAG, "Long press callback registered successfully");
    } else {
        ESP_LOGW(TAG, "Attempted to register NULL long press callback");
    }
}

void button_register_double_click_cb(button_event_cb_t callback)
{
    if (callback != NULL) {
        double_click_cb = callback;
        ESP_LOGI(TAG, "Double click callback registered successfully");
    } else {
        ESP_LOGW(TAG, "Attempted to register NULL double click callback");
    }
}

// 默认事件处理函数
static void default_short_press_handler(void)
{
    ESP_LOGI(TAG, "Default action: Showing honeycomb menu");
    // 确保菜单函数存在再调用
    show_honeycomb_menu();
}

static void default_long_press_handler(void)
{
    ESP_LOGI(TAG, "Default action: Resetting honeycomb menu to center");
    // 确保重置函数存在再调用
    reset_honeycomb_menu();
}

static void default_double_click_handler(void)
{
    ESP_LOGI(TAG, "Default action: Switch to main screen (placeholder)");
    // 这里可以添加切换回主界面的逻辑
    // 例如：
    // if (main_screen != NULL) {
    //     lv_scr_load(main_screen);
    // } else {
    //     ESP_LOGW(TAG, "Main screen not available");
    // }
}

void init_boot_btn(void)
{
    ESP_LOGI(TAG, "Initializing BOOT button (GPIO%d)...", BOOT_BTN_GPIO);
    
    // 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOOT_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", BOOT_BTN_GPIO, esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "GPIO%d configured successfully", BOOT_BTN_GPIO);
    
    // 验证GPIO初始状态
    int initial_level = gpio_get_level(BOOT_BTN_GPIO);
    ESP_LOGI(TAG, "Initial button state: %s", initial_level ? "RELEASED" : "PRESSED");
    
    // 设置默认回调函数
    short_press_cb = default_short_press_handler;
    long_press_cb = default_long_press_handler;
    double_click_cb = default_double_click_handler;
    ESP_LOGI(TAG, "Default button callbacks configured");
    
    // 创建按钮检测任务
    BaseType_t task_ret = xTaskCreate(
        boot_btn_task,
        "boot_btn_task",
        BTN_TASK_STACK_SIZE,
        NULL,
        BTN_TASK_PRIORITY,
        &btn_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create boot button task (error: %d)", task_ret);
        return;
    }
    
    ESP_LOGI(TAG, "Boot button task created successfully");
    ESP_LOGI(TAG, "Button configuration:");
    ESP_LOGI(TAG, "  - Debounce time: %d ms", BTN_DEBOUNCE_TIME_MS);
    ESP_LOGI(TAG, "  - Long press threshold: %d ms", BTN_LONG_PRESS_TIME_MS);
    ESP_LOGI(TAG, "  - Double click window: %d ms", DOUBLE_CLICK_TIME_MS);
    ESP_LOGI(TAG, "  - Poll interval: %d ms", BTN_POLL_INTERVAL_MS);
    ESP_LOGI(TAG, "Button functions:");
    ESP_LOGI(TAG, "  - Short press: Show honeycomb menu");
    ESP_LOGI(TAG, "  - Long press: Reset menu position");
    ESP_LOGI(TAG, "  - Double click: Return to main screen");
    ESP_LOGI(TAG, "Boot button initialization completed successfully");
}

// 获取按钮当前状态（用于调试）
button_state_t get_button_state(void)
{
    return btn_state;
}

// 获取按钮状态字符串（用于调试）
const char* get_button_state_string(void)
{
    switch (btn_state) {
        case BTN_STATE_IDLE: return "IDLE";
        case BTN_STATE_PRESSED: return "PRESSED";
        case BTN_STATE_HELD: return "HELD";
        case BTN_STATE_RELEASED: return "RELEASED";
        default: return "UNKNOWN";
    }
}

// 获取按钮统计信息
void get_button_statistics(button_stats_t *stats)
{
    if (stats == NULL) {
        ESP_LOGW(TAG, "Button statistics pointer is NULL");
        return;
    }
    
    // 这里可以添加统计信息，比如按键次数等
    stats->current_state = btn_state;
    stats->is_waiting_double_click = waiting_for_double_click;
    stats->last_press_duration = (btn_state == BTN_STATE_PRESSED || btn_state == BTN_STATE_HELD) 
                                 ? (get_time_ms() - btn_press_start_time) : 0;
    stats->task_handle = btn_task_handle;
}

// 重置按钮状态（用于调试或故障恢复）
void reset_button_state(void)
{
    ESP_LOGW(TAG, "Resetting button state to IDLE");
    btn_state = BTN_STATE_IDLE;
    btn_press_start_time = 0;
    btn_long_press_handled = false;
    waiting_for_double_click = false;
    last_click_time = 0;
}

// 检查按钮任务是否正在运行
bool is_button_task_running(void)
{
    if (btn_task_handle == NULL) {
        return false;
    }
    
    eTaskState task_state = eTaskGetState(btn_task_handle);
    return (task_state != eDeleted && task_state != eInvalid);
}