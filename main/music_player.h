#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>

// 播放状态枚举
typedef enum {
    PLAYER_STATE_STOPPED,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED
} player_state_t;

// 音乐文件信息结构
typedef struct {
    char filename[128];
    char filepath[256];
    char title[64];
    char artist[32];
    uint32_t duration;  // 秒
    size_t file_size;
} music_file_t;

// 播放器控制结构
typedef struct {
    music_file_t* playlist;
    int playlist_count;
    int current_index;
    player_state_t state;
    uint32_t current_position;  // 当前播放位置（秒）
    bool shuffle;
    bool repeat;
} music_player_t;

// 外部函数声明
lv_obj_t* music_player_create(void);
esp_err_t music_player_scan_files(void);
esp_err_t music_player_play(int index);
esp_err_t music_player_pause(void);
esp_err_t music_player_resume(void);
esp_err_t music_player_stop(void);
esp_err_t music_player_next(void);
esp_err_t music_player_previous(void);
void music_player_set_visible(bool visible);
lv_obj_t* get_music_player_screen(void);

#endif // MUSIC_PLAYER_H
