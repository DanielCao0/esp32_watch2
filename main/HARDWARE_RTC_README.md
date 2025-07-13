# ESP32-S3 硬件RTC使用说明

## 概述
本项目实现了基于ESP32-S3内置硬件RTC的完整时间管理系统。硬件RTC相比软件RTC的优势：

- **掉电保持**：在深度睡眠或断电后能保持时间运行（需要备用电池）
- **低功耗**：RTC域独立供电，功耗极低
- **高精度**：使用外部32.768kHz晶振，精度更高
- **系统集成**：与ESP32的系统时间完美集成

## 文件结构

### `hardware_rtc.h`
- 包含所有RTC相关的数据结构和函数声明
- 定义了`hardware_rtc_time_t`时间结构体
- 提供了完整的API接口

### `hardware_rtc.c`
- 实现了所有RTC功能
- 支持时间设置、获取、格式化
- 提供时区支持和NTP同步接口

## 主要功能

### 1. 初始化
```c
esp_err_t hardware_rtc_init(void);
```
- 初始化硬件RTC
- 检查时间状态
- 设置默认时区（北京时间 UTC+8）

### 2. 时间设置和获取
```c
// 设置时间
hardware_rtc_time_t time = {
    .year = 2025,
    .month = 7,
    .day = 13,
    .hour = 14,
    .minute = 30,
    .second = 0,
    .weekday = hardware_rtc_calculate_weekday(2025, 7, 13)
};
hardware_rtc_set_time(&time);

// 获取时间
hardware_rtc_time_t current_time;
hardware_rtc_get_time(&current_time);
```

### 3. 时间格式化
支持多种格式输出：
```c
char buffer[128];

// 标准日期时间: "2025-07-13 14:30:00"
hardware_rtc_format_time(&time, buffer, sizeof(buffer), "datetime");

// 仅时间: "14:30:00"
hardware_rtc_format_time(&time, buffer, sizeof(buffer), "time");

// 仅日期: "2025-07-13"
hardware_rtc_format_time(&time, buffer, sizeof(buffer), "date");

// 中文格式: "2025年07月13日 周六 14:30:00"
hardware_rtc_format_time(&time, buffer, sizeof(buffer), "chinese");

// ISO8601格式: "2025-07-13T14:30:00+08:00"
hardware_rtc_format_time(&time, buffer, sizeof(buffer), "iso8601");
```

### 4. NTP同步
```c
// 从NTP时间戳同步
hardware_rtc_sync_from_ntp(ntp_timestamp_us);

// 从系统时间同步
hardware_rtc_sync_from_system();
```

### 5. 时区管理
```c
// 设置时区（北京时间）
hardware_rtc_set_timezone(8);

// 设置时区（纽约时间）
hardware_rtc_set_timezone(-5);
```

### 6. 实用工具
```c
// 获取运行时长
uint64_t uptime = hardware_rtc_get_uptime_seconds();

// 获取Unix时间戳
time_t timestamp = hardware_rtc_get_timestamp();

// 计算星期几
int weekday = hardware_rtc_calculate_weekday(2025, 7, 13);

// 获取星期几名称
const char* day_cn = hardware_rtc_get_weekday_name_cn(weekday);  // "周六"
const char* day_en = hardware_rtc_get_weekday_name_en(weekday);  // "Saturday"

// 检查闰年
bool is_leap = hardware_rtc_is_leap_year(2024);  // true
```

## 在主函数中的集成

### 初始化顺序
```c
void app_main(void)
{
    // ... LCD/LVGL初始化 ...
    
    // 1. 初始化WiFi（为NTP同步做准备）
    wifi_connect_init();
    
    // 2. 初始化硬件RTC
    hardware_rtc_init();
    
    // 3. 初始化时钟系统（用于NTP同步）
    clock_init();
    
    // ... 其他初始化 ...
}
```

### 使用示例
```c
// 检查RTC状态
hardware_rtc_info_t rtc_info;
hardware_rtc_get_info(&rtc_info);

// 显示当前时间
hardware_rtc_time_t current_time;
if (hardware_rtc_get_time(&current_time) == ESP_OK) {
    char time_str[64];
    hardware_rtc_format_time(&current_time, time_str, sizeof(time_str), "chinese");
    ESP_LOGI(TAG, "当前时间: %s", time_str);
}
```

## 与NTP时钟系统配合

硬件RTC与现有的`clock.c`模块完美配合：

1. **clock.c** 负责WiFi连接后的NTP时间同步
2. **hardware_rtc.c** 负责本地时间管理和持久化
3. NTP同步成功后，自动更新硬件RTC
4. 断网或重启后，硬件RTC继续提供准确时间

## 电源管理

### 深度睡眠支持
```c
// 进入深度睡眠前，RTC会自动保持运行
esp_deep_sleep_start();

// 唤醒后，时间仍然准确
hardware_rtc_time_t wake_time;
hardware_rtc_get_time(&wake_time);
```

### 备用电池
为了在完全断电时保持时间，建议：
- 在RTC域连接3V纽扣电池（CR2032）
- 电池通过二极管连接到VDD3P3_RTC引脚
- 正常供电时电池不工作，断电时自动切换

## 注意事项

1. **首次使用**：需要设置时间或等待NTP同步
2. **时区设置**：根据地理位置设置正确的时区偏移
3. **精度**：依赖外部32.768kHz晶振，PCB布局影响精度
4. **功耗**：RTC域功耗约2μA，对电池寿命影响很小

## 调试信息

程序运行时会输出详细的RTC状态信息：
```
I (1234) hardware_rtc: Hardware RTC initialized successfully
I (1235) hardware_rtc:   Boot time: 1234567 us
I (1236) hardware_rtc:   Timezone offset: UTC+8
I (1237) hardware_rtc:   RTC status: RUNNING
I (1238) example: Current RTC time: 2025-07-13 14:30:00
I (1239) example: Current time (Chinese): 2025年07月13日 周六 14:30:00
```

这个硬件RTC系统为ESP32-S3智能手表项目提供了可靠的时间基础，支持断电保持、高精度计时和多种显示格式。
