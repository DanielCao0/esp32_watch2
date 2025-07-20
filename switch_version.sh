#!/bin/bash

# ESP32 Watch 项目编译切换脚本
# 用于在原版本和音乐测试版本之间切换

PROJECT_DIR="/home/daniel/diy_watch/esp32_watch2"
MAIN_DIR="$PROJECT_DIR/main"

echo "ESP32 Watch 项目编译切换工具"
echo "==============================="

if [ "$1" = "test" ]; then
    echo "🎵 切换到音乐测试版本..."
    echo "   - 使用 test_music_main.c 作为主文件"
    echo "   - 只编译音乐播放相关模块"
    echo "   - 无UI界面，专注音频测试"
    
    # 备份当前 CMakeLists.txt（如果还没有备份）
    if [ ! -f "$MAIN_DIR/CMakeLists.txt.backup" ]; then
        cp "$MAIN_DIR/CMakeLists.txt" "$MAIN_DIR/CMakeLists.txt.backup"
        echo "   ✅ 已备份原 CMakeLists.txt"
    fi
    
    # 创建测试版 CMakeLists.txt
    cat > "$MAIN_DIR/CMakeLists.txt" << 'EOF'
file(GLOB_RECURSE SRC_UI ui/*.c)

idf_component_register(
    SRCS "test_music_main.c" "sdcard.c" "music_player.c"
        
    INCLUDE_DIRS "." "ui"
    REQUIRES lwip esp_netif esp_wifi driver esp_timer nvs_flash fatfs sdmmc
)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
EOF
    
    echo "   ✅ 已切换到音乐测试版本"
    echo ""
    echo "🎵 现在可以编译和烧录："
    echo "   cd $PROJECT_DIR"
    echo "   idf.py build"
    echo "   idf.py flash monitor"
    echo ""
    echo "📝 测试要求："
    echo "   - 在SD卡中创建 /music/ 目录"
    echo "   - 将测试用的 a.mp3 文件放入该目录"
    echo "   - 连接音频输出设备（扬声器或耳机）"
    
elif [ "$1" = "original" ]; then
    echo "🖥️  切换到原版本..."
    echo "   - 使用 main.c 作为主文件"
    echo "   - 完整的UI界面和所有功能"
    
    # 恢复原版 CMakeLists.txt
    if [ -f "$MAIN_DIR/CMakeLists.txt.backup" ]; then
        cp "$MAIN_DIR/CMakeLists.txt.backup" "$MAIN_DIR/CMakeLists.txt"
        echo "   ✅ 已恢复原 CMakeLists.txt"
    else
        echo "   ❌ 未找到备份文件，请手动恢复"
        exit 1
    fi
    
    echo "   ✅ 已切换到原版本"
    echo ""
    echo "🖥️  现在可以编译和烧录："
    echo "   cd $PROJECT_DIR"
    echo "   idf.py build"
    echo "   idf.py flash monitor"
    
elif [ "$1" = "status" ]; then
    echo "📊 当前状态检查..."
    
    if [ -f "$MAIN_DIR/test_music_main.c" ]; then
        echo "   ✅ 音乐测试文件存在"
    else
        echo "   ❌ 音乐测试文件不存在"
    fi
    
    if [ -f "$MAIN_DIR/CMakeLists.txt.backup" ]; then
        echo "   ✅ 原版配置已备份"
    else
        echo "   ⚠️  原版配置未备份"
    fi
    
    echo ""
    echo "📄 当前 CMakeLists.txt 内容："
    echo "----------------------------------------"
    if grep -q "test_music_main.c" "$MAIN_DIR/CMakeLists.txt" && ! grep -q "main.c" "$MAIN_DIR/CMakeLists.txt"; then
        echo "   🎵 当前使用：音乐测试版本"
    elif grep -q "main.c" "$MAIN_DIR/CMakeLists.txt"; then
        echo "   🖥️  当前使用：原版本"
    else
        echo "   ❓ 无法识别当前版本"
    fi
    
else
    echo ""
    echo "用法："
    echo "  $0 test      - 切换到音乐测试版本"
    echo "  $0 original  - 切换到原版本"
    echo "  $0 status    - 查看当前状态"
    echo ""
    echo "示例："
    echo "  $0 test                    # 切换到音乐测试版本"
    echo "  cd $PROJECT_DIR            # 进入项目目录"
    echo "  idf.py build               # 编译项目"
    echo "  idf.py flash monitor       # 烧录并监控"
    echo ""
    echo "  $0 original                # 切换回原版本"
    echo "  idf.py build               # 重新编译"
    echo ""
    echo "注意："
    echo "  - 音乐测试版本需要SD卡中有 /music/a.mp3 文件"
    echo "  - 音乐测试版本没有UI界面，通过串口输出查看状态"
    echo "  - 切换版本后需要重新编译项目"
fi
