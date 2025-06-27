SET(SOURCES screens/ui_ScreenMenu.c
    screens/ui_ScreenHomeImg.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_icon_alarm_png.c
    images/ui_img_icon_clock_png.c
    images/ui_img_icon_itunes_png.c
    images/ui_img_icon_camera_png.c
    images/ui_img_icon_mail_png.c
    images/ui_img_icon_maps_png.c
    images/ui_img_icon_phone_png.c
    images/ui_img_icon_photos_png.c
    images/ui_img_icon_pulse_png.c
    images/ui_img_icon_reminders_png.c
    images/ui_img_icon_settings_png.c
    images/ui_img_icon_stocks_png.c
    images/ui_img_icon_timer_png.c
    images/ui_img_icon_wallet_png.c
    images/ui_img_icon_weather_png.c
    images/ui_img_icon_workout_png.c
    images/ui_img_icon_world_png.c
    images/ui_img_icon_imessage_png.c
    images/ui_img_icon_calendar_png.c
    images/ui_img_img_img1_png.c)

add_library(ui ${SOURCES})
