#define DEBUG
#include "ui_layout.h"
#include "ui_image.h"
#include "common/takephoto.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "lvgl.h"
#include "page_all.h"
#include "ui_common.h"
#include "voiceplay.h"
#include <stdio.h>
#include <time.h>

lv_obj_t* label_Vedio_Durtime_s; //录像时长
lv_obj_t* obj_vedio_s; //底层窗口
lv_obj_t *dot_red_s;              //闪烁红点
lv_obj_t *video_flash_s;          //flash
lv_obj_t *img_sdonline_s = NULL;
lv_obj_t *img_batter_s = NULL;
lv_obj_t* label_available_video_s = NULL; //剩余录像时长
static lv_obj_t* img_effect_s = NULL; //特效图标
static const char* g_flashlight_img[] = { "flash_off.png", "flash_on.png" };
uint8_t g_flash_led_index = 0;

// lv_obj_t *label_datatime_s = NULL;
extern lv_style_t ttf_font_24;
extern lv_style_t ttf_font_16;
static lv_timer_t *date_timer_s = NULL; //时间更新定时器

extern const char *effect_style_small[];//特效图片数组

extern uint8_t is_start_video;     //录像状态
extern bool is_video_mode;
extern char *batter_image_big[];
extern int32_t g_batter_image_index;

static void video_zoomin_key_cb(void);//w按键回调
static void video_zoomout_key_cb(void);//t按键回调
// left按键处理回调函数
static void video_left_callback(void);
// right按键处理回调函数
static void video_right_callback(void);
void animCompleted_objDel_cb(lv_anim_t* a);
// 清理视频页面资源的通用函数
static void cleanup_vedio_page_resources(void)
{

    takephoto_unregister_all_callback();//取消所有按键回调
    if(is_start_video == VEDIO_START) {
        is_start_video = VEDIO_STOP;
        MESSAGE_S Msg  = {0};
        Msg.topic = EVENT_MODEMNG_STOP_REC;
        MODEMNG_SendMessage(&Msg);
        enable_touch_events(); // 停止录像，恢复TP事件
        // 停止录像时，开启音频输出
        UI_VOICEPLAY_Init();
    }
    delete_batter_tips_mbox(); // 清除低电量弹窗

    // 清理定时器和动画，避免在页面切换时访问已销毁的对象
    if (date_timer_s != NULL) {
        lv_timer_delete(date_timer_s);
        date_timer_s = NULL;
    }
    //删除关于特效的资源
    delete_all_handle();
    delete_viewfinder();//销毁取景框
    delete_zoom_bar(); //销毁zoombar
    // 释放缩放相关资源
    delete_zoombar_timer_handler();
    enable_focus();
}

//参数动态更新回调
static void video_var_dynamic_update(lv_timer_t *timer)
{
    // lv_ui_t *ui  = (lv_ui_t *)lv_timer_get_user_data(timer);
    time_t now   = time(NULL);
    struct tm *t = localtime(&now);
    static uint32_t total_seconds = 0;
    // MLOG_DBG("%s[%d]   %d...\n",__func__,__LINE__,lv_obj_is_valid(obj_vedio_s));
    {
        // 检查对象有效性，避免在页面切换时访问已销毁的对象
        if (!lv_obj_is_valid(obj_vedio_s)) {
            return;
        }

        //如果录制的过程中录制满了，则停止录像,并恢复TP事件
        if (ui_event_type == EVT_SDCARD_SPACE_FULL && is_start_video == VEDIO_START) {
            is_start_video = VEDIO_STOP;
            MESSAGE_S Msg = { 0 };
            Msg.topic = EVENT_MODEMNG_STOP_REC;
            MODEMNG_SendMessage(&Msg);
            enable_touch_events(); // 停止录像，恢复TP事件
            // 停止录像时，开启音频输出
            UI_VOICEPLAY_Init();
        }
        // // 更新日期
        // lv_label_set_text_fmt(label_datatime_s, "%04d/%02d/%02d", t->tm_year + 1900, t->tm_mon + 1,
        //                       t->tm_mday);
        if(is_start_video == VEDIO_STOP)
        {
            if(getSelect_Index() == TIME_FLAG_OFF && !lv_obj_has_flag(label_Vedio_Durtime_s,LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
            }
            // 更新时间
            lv_label_set_text_fmt(label_Vedio_Durtime_s, "%04d/%02d/%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        //电池等级更新
        show_image(img_batter_s, batter_image_big[g_batter_image_index]);
        lv_label_set_text_fmt(label_available_video_s, "%s", video_Calculateremainingvideo());
        if(ui_common_cardstatus()) {
            show_image(img_sdonline_s, "icon_card_online.png");
        } else {
            show_image(img_sdonline_s, "icon_card_offline.png");
        }
        //录像状态
        switch(is_start_video)
        {
            case VEDIO_STOP:
            {
                if(!lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
                total_seconds = 0;
            }  ;break;
            case VEDIO_START:
            {
                if(!lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
                else if(lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_remove_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
                uint8_t hour = (total_seconds / 3600);
                uint8_t min = (total_seconds%3600)/60;
                uint8_t sec = total_seconds%60;
                total_seconds++;

                lv_label_set_text_fmt(label_Vedio_Durtime_s, "%02d:%02d:%02d",hour, min, sec);
                lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);

            };break;
        }
    }
}

//渐隐动画完成回调
void animCompleted_objDel_cb(lv_anim_t *a)
{
    //移除标志
    if(getSelect_Index() == TIME_FLAG_ON) {
        lv_obj_remove_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_resume(date_timer_s);
    delete_all_handle();
}

// ok按键处理回调函数
static void video_sesor_switch_completed_callback(void)
{
    enable_touch_events(); // 恢复触摸
    enable_hardware_input_device(0);
    enable_hardware_input_device(1);
}

static void buttonVedio_All_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int Click_index = (int)lv_event_get_user_data(e);
    MLOG_DBG("code:%d, Click_index:%d\n", code, Click_index);

    switch(code) {
        case LV_EVENT_CLICKED: {
            // 清理视频页面资源
            if (Click_index != 4 && get_modechange_completed())
                cleanup_vedio_page_resources();
            if(Click_index == 1) {
                ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                      false, true);
            } else if(Click_index == 2) {
                if (!get_modechange_completed()) {
                    return;
                }
                usleep(150000); // 延时100ms，确保按键音效播放完毕。
                // 进入拍照模式
                set_modechange_completed(false);
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PHOTO;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                // 使能对焦
                enable_focus();
                is_video_mode = false;
                ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    false, true);
            } else if (Click_index == 3) {
                MESSAGE_S Msg = { 0 };
                reset_effect();
                takephoto_cancel_focus();
                // 通知mode关闭时要关闭sensor
                Msg.topic = EVENT_MODEMNG_SENSOR_STATE;
                Msg.arg1      = 1;
                MODEMNG_SendMessage(&Msg);
                memset(&Msg, 0, sizeof(MESSAGE_S));
                // 进入BOOT模式
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1  = WORK_MODE_BOOT;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                is_video_mode = false;
                ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                                      true);
            } else if (Click_index == 4) {
                MLOG_DBG("切换前后摄像头\n");
                MESSAGE_S Msg = {0};
                Msg.topic     = EVENT_MODEMNG_SETTING;
                Msg.arg1      = PARAM_MENU_SENSOR_SWITCH;
                MODEMNG_SendMessage(&Msg);

                disable_touch_events(); // 禁用触摸
                disable_hardware_input_device(0);
                disable_hardware_input_device(1);
                completed_register_cb(video_sesor_switch_completed_callback);

                // 复位闪光灯
                g_flash_led_index = 0;
                show_image(video_flash_s, g_flashlight_img[g_flash_led_index]);
                // 复位缩放
                set_zoom_level(1);
            } else if (Click_index == 5) {
                MLOG_DBG("点击相册按钮，进入相册\n");
                extern bool g_is_photo_mode;
                g_is_photo_mode = false;
                // 进入回放模式
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PLAYBACK;
                MODEMNG_SendMessage(&Msg);
                ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
            }
            break;
        }
        default: break;
    }
}

void video_effect_scr_delete(void)
{
    delete_all_handle();
    lv_timer_ready(date_timer_s);
    lv_timer_resume(date_timer_s);
}

//特效选择回调
static void vedioEffect_Select_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED:
        {
            if(get_is_effect_exist() == true) {
                delete_all_handle();
                lv_timer_resume(date_timer_s);
            } else {
                // 添加隐藏时间标志
                lv_timer_pause(date_timer_s);
                // 创建滚动列表
                float_effect_creat(img_effect_s,obj_vedio_s);
                // 创建控件并启动渐渐隐藏动画
                create_gradually_hide_anim(animCompleted_objDel_cb,8000);
            }
            // Update current screen layout.
            lv_obj_update_layout(obj_vedio_s);
        } break;
        default: break;
    }
}

// 菜单按键处理回调函数
static void key_takephoto_menu_callback(void)
{
    MLOG_DBG("进入录像模式设置页面\n");

    // 清理视频页面资源
    cleanup_vedio_page_resources();

    ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
        false, true);
}

// UP按键处理回调函数
static void video_up_callback(void)
{
    int32_t sensor_type = takephoto_get_current_sensor_type();
    int32_t usb_type = check_usb_charger_mode();

    if (sensor_type == TAKEPHOTO_SENSOR_SELFIE || usb_type == USB_MODE_ADAPTER) {
        MLOG_WARN("selfie sensor does not support flash control\n");
        return;
    }

    /* 当前闪光灯是关闭的, 想要打开, 但是电量不够; 是打开状态则可以关闭 */
    if (g_flash_led_index == 0 && !check_battery_for_flash_led(obj_vedio_s)) {
        return;
    }

    /* 0: close 1:open */
    g_flash_led_index = !g_flash_led_index;
    MESSAGE_S Msg = { 0 };
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1      = PARAM_MENU_FLASH_LED;
    Msg.arg2 = g_flash_led_index;
    MODEMNG_SendMessage(&Msg);
    show_image(video_flash_s, g_flashlight_img[g_flash_led_index]);
}

// 模式切换按键处理回调函数
static void key_takephoto_mode_callback(void)
{
    MLOG_DBG("模式切换，进入拍照模式\n");
    if (!get_modechange_completed()) {
        return;
    }
    // 清理视频页面资源
    cleanup_vedio_page_resources();

    set_modechange_completed(false);
    usleep(150000); // 延时100ms，确保按键音效播放完毕。
    MESSAGE_S Msg = { 0 };
    Msg.topic = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1 = WORK_MODE_PHOTO;
    MODEMNG_SendMessage(&Msg);
    set_modechange_completed(false);
    // 复位缩放
    set_zoom_level(1);
    // 使能对焦
    enable_focus();
    is_video_mode = false;
    ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                            false, true);
}

static void vieo_key_down_callback(void)
{
    create_simple_delete_dialog(NULL);//创建确认浮窗
}

// 长按菜单按键处理回调函数
static void video_long_menu_callback(void)
{
    MLOG_DBG("长按退出视频模式\n");
    cleanup_vedio_page_resources();
    reset_effect();
    MESSAGE_S Msg = { 0 };
    takephoto_cancel_focus();
    // 通知mode关闭时要关闭sensor
    Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
    Msg.arg1      = 1;
    MODEMNG_SendMessage(&Msg);
    memset(&Msg, 0, sizeof(MESSAGE_S));
    // 进入BOOT模式
    Msg.topic     = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1      = WORK_MODE_BOOT;
    MODEMNG_SendMessage(&Msg);
    // 复位缩放
    set_zoom_level(1);
    is_video_mode = false;
    ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

// ok按键处理回调函数
static void key_takephoto_ok_callback(void)
{
    if (get_is_effect_exist() == false) {
        lv_timer_pause(date_timer_s);
        // 创建滚动列表
        float_effect_creat(img_effect_s, obj_vedio_s);
        create_gradually_hide_anim(animCompleted_objDel_cb, 8000);
    } else {
        set_effect_ok();
        lv_timer_resume(date_timer_s);
    }
}

static void key_takephoto_power_callback(void)
{
    MESSAGE_S Msg = { 0 };
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1 = PARAM_MENU_SENSOR_SWITCH;
    MODEMNG_SendMessage(&Msg);

    disable_touch_events(); // 禁用触摸
    disable_hardware_input_device(0);
    disable_hardware_input_device(1);
    completed_register_cb(video_sesor_switch_completed_callback);
    // 复位缩放
    set_zoom_level(1);
    // 复位闪光灯
    g_flash_led_index = 0;
    show_image(video_flash_s, g_flashlight_img[g_flash_led_index]);
}

// 长按模式按键处理回调函数
static void video_long_mode_callback(void)
{
    MLOG_DBG("长按模式按键，切换前后摄像头\n");
    MESSAGE_S Msg = {0};
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1  = PARAM_MENU_SENSOR_SWITCH;
    MODEMNG_SendMessage(&Msg);
    // 复位缩放
    set_zoom_level(1);
    // 复位闪光灯
    g_flash_led_index = 0;
    show_image(video_flash_s, g_flashlight_img[g_flash_led_index]);
}

static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_GESTURE: {
            // 获取手势方向，需要 TP 驱动支持
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch (dir) {
            case LV_DIR_RIGHT: {
                MESSAGE_S Msg = { 0 };
                reset_effect();
                takephoto_cancel_focus();
                // 通知mode关闭时要关闭sensor
                Msg.topic = EVENT_MODEMNG_SENSOR_STATE;
                Msg.arg1 = 1;
                MODEMNG_SendMessage(&Msg);
                memset(&Msg, 0, sizeof(MESSAGE_S));
                // 进入BOOT模式
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_BOOT;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                is_video_mode = false;
                ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    false, true);
                cleanup_vedio_page_resources();
            }
                default: break;
                }
            break;
        }
        default: break;
    }
}

void Home_Vedio(lv_ui_t* ui)
{
    MLOG_DBG("loading obj_vedio_s...\n");
    extern uint8_t g_last_scr_mode;
    g_last_scr_mode = 2;

    obj_vedio_s = lv_obj_create(NULL);
    // 注册删除回调：屏幕被 auto_del 删除时自动置空指针，避免悬垂指针被 lv_obj_is_valid 误判导致误删当前活动屏幕
    lv_obj_null_on_delete(&obj_vedio_s);
    lv_obj_set_size(obj_vedio_s, H_RES, V_RES);

    lv_obj_set_scrollbar_mode(obj_vedio_s, LV_SCROLLBAR_MODE_OFF); // 禁用滚动条，默认 LV_SCROLLBAR_MODE_AUTO
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj_vedio_s, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_vedio_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT); // 背景颜色
    lv_obj_set_style_bg_grad_dir(obj_vedio_s, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);    // 无渐变色
    lv_obj_add_event_cb(obj_vedio_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // 录像模式
    lv_obj_t* img_mode = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_mode, LV_ALIGN_TOP_LEFT, UI_CAMERA_MARGIN, UI_CAMERA_TOP);
    lv_obj_set_size(img_mode, UI_CAMERA_ICON_SIZE, UI_CAMERA_ICON_SIZE);
    show_image(img_mode, "shexiangmoshi.png");
    lv_obj_add_event_cb(img_mode, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)2);

    // res
    lv_obj_t* img_res = ui_image_create(obj_vedio_s);
    lv_obj_align(img_res, LV_ALIGN_TOP_LEFT, 64, UI_CAMERA_TOP);
    lv_obj_set_size(img_res, 64, UI_CAMERA_ICON_SIZE);
    show_image(img_res, video_getRes_Icon());
    lv_obj_set_style_image_recolor(img_res, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_res, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_image_recolor(img_res, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_update_layout(img_res);

    //闪光灯
    video_flash_s = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(video_flash_s, LV_ALIGN_TOP_LEFT, 140, UI_CAMERA_TOP);
    lv_obj_set_size(video_flash_s, UI_CAMERA_ICON_SIZE, UI_CAMERA_ICON_SIZE);
    g_flash_led_index = 0;
    show_image(video_flash_s, g_flashlight_img[g_flash_led_index]);

    // 白平衡
    const char* whitebalanc[] = { "WAB AUTo.png", "白平衡太阳.png", "白平衡阴天.png", "白平衡灯泡.png",
        "白平衡日光灯.png" };
    lv_obj_t* img_whitebalanc = ui_image_create(obj_vedio_s);
    lv_obj_align(img_whitebalanc, LV_ALIGN_TOP_LEFT, UI_CAMERA_MARGIN, UI_CAMERA_SECOND_ROW);
    lv_obj_set_size(img_whitebalanc, UI_CAMERA_ICON_SIZE, UI_CAMERA_ICON_SIZE);
    if (getwhiBlance_Index() == 0)
        lv_obj_add_flag(img_whitebalanc, LV_OBJ_FLAG_HIDDEN);
    show_image(img_whitebalanc, whitebalanc[getwhiBlance_Index()]);
    lv_obj_set_style_image_recolor(img_whitebalanc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_whitebalanc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_image_recolor(img_whitebalanc, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_update_layout(img_whitebalanc);
    // 曝光
    static const char* photo_EV_s[] = {
        "ev3.png",
        "EV2.png",
        "EV1.png",
        "EV00.png",
        "EV11.png",
        "EV22.png",
        "EV33.png",
    };
    lv_obj_t* img_ev = ui_image_create(obj_vedio_s);
    lv_obj_align(img_ev, LV_ALIGN_TOP_LEFT, 64, UI_CAMERA_SECOND_ROW);
    lv_obj_set_size(img_ev, UI_CAMERA_ICON_SIZE, UI_CAMERA_ICON_SIZE);
    if (get_EV_Level() == 3)
        lv_obj_add_flag(img_ev, LV_OBJ_FLAG_HIDDEN);
    show_image(img_ev, photo_EV_s[get_EV_Level()]);
    lv_obj_update_layout(img_ev);

    // sd
    img_sdonline_s = ui_image_create(obj_vedio_s);
    lv_obj_align(img_sdonline_s, LV_ALIGN_TOP_RIGHT, -56, UI_CAMERA_TOP + 4);
    lv_obj_set_size(img_sdonline_s, UI_CAMERA_STATUS_SIZE, UI_CAMERA_STATUS_SIZE);
    if (ui_common_cardstatus()) {
        show_image(img_sdonline_s, "icon_card_online.png");
    } else {
        show_image(img_sdonline_s, "icon_card_offline.png");
    }

    // batter
    img_batter_s = ui_image_create(obj_vedio_s);
    lv_obj_align(img_batter_s, LV_ALIGN_TOP_RIGHT, -UI_CAMERA_MARGIN, UI_CAMERA_TOP + 4);
    lv_obj_set_size(img_batter_s, UI_CAMERA_STATUS_SIZE, UI_CAMERA_STATUS_SIZE);
    show_image(img_batter_s, batter_image_big[g_batter_image_index]);

    // 相册按钮
    lv_obj_t* img_album = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_album, LV_ALIGN_TOP_RIGHT, -UI_CAMERA_MARGIN, UI_CAMERA_SECOND_ROW);
    lv_obj_set_size(img_album, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    show_image(img_album, "home_相册.png");
    lv_obj_add_event_cb(img_album, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)5);

    // 退出按钮
    lv_obj_t* img_exit = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_exit, LV_ALIGN_BOTTOM_RIGHT, -UI_CAMERA_MARGIN, -UI_CAMERA_MARGIN);
    lv_obj_set_size(img_exit, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    show_image(img_exit, "exit.png");
    lv_obj_set_style_image_recolor(img_exit, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(img_exit, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(img_exit, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)3);

    lv_obj_t* flip = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(flip, LV_ALIGN_BOTTOM_RIGHT, -UI_CAMERA_SIDE_OFFSET, -UI_CAMERA_MARGIN);
    lv_obj_set_size(flip, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    show_image(flip, "翻转镜头.png");
    lv_obj_add_event_cb(flip, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)4);

    // menu
    lv_obj_t* img_menu = ui_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_menu, LV_ALIGN_BOTTOM_LEFT, UI_CAMERA_MARGIN, -UI_CAMERA_MARGIN);
    lv_obj_set_size(img_menu, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    show_image(img_menu, "menu.png");
    lv_obj_add_event_cb(img_menu, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)1);

    // 滤镜
    lv_obj_t* btn_effect = lv_button_create(obj_vedio_s);
    lv_obj_align(btn_effect, LV_ALIGN_BOTTOM_LEFT, UI_CAMERA_SIDE_OFFSET, -UI_CAMERA_MARGIN);
    lv_obj_set_size(btn_effect, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    lv_obj_set_style_bg_opa(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // 透明背景
    lv_obj_set_style_pad_all(btn_effect, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_effect, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_effect, vedioEffect_Select_event_cb, LV_EVENT_CLICKED, NULL);

    img_effect_s = ui_image_create(btn_effect);
    lv_obj_set_size(img_effect_s, UI_CAMERA_TOOL_SIZE, UI_CAMERA_TOOL_SIZE);
    lv_obj_align(img_effect_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(img_effect_s, 0, LV_STATE_DEFAULT);
    show_image(img_effect_s, effect_style_small[geteffect_index()]);

    //创建缩放UI
    create_zoom_bar(obj_vedio_s);

    // 闪烁圆点
    dot_red_s = lv_obj_create(obj_vedio_s);
    lv_obj_set_size(dot_red_s, UI_X(24), UI_X(24));
    lv_obj_align(dot_red_s, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(dot_red_s, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot_red_s, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot_red_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(dot_red_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(dot_red_s, 0, LV_STATE_DEFAULT);

    create_viewfinder(obj_vedio_s);

    label_Vedio_Durtime_s = lv_label_create(obj_vedio_s);
    lv_obj_add_style(label_Vedio_Durtime_s, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(label_Vedio_Durtime_s, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label_Vedio_Durtime_s, UI_WIDTH - 2 * UI_CAMERA_MARGIN);
    lv_obj_set_style_text_align(label_Vedio_Durtime_s, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_Vedio_Durtime_s, LV_ALIGN_BOTTOM_MID, 0, -UI_CAMERA_INFO_BOTTOM);
    if(getSelect_Index() == TIME_FLAG_OFF) {
        lv_obj_add_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
    }

    // 剩余录像时间
    label_available_video_s = lv_label_create(obj_vedio_s);
    lv_obj_add_style(label_available_video_s, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_available_video_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(label_available_video_s, "%s", video_Calculateremainingvideo());
    lv_label_set_long_mode(label_available_video_s, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label_available_video_s, UI_CAMERA_COUNT_WIDTH);
    lv_obj_set_style_text_align(label_available_video_s, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(label_available_video_s, LV_ALIGN_TOP_RIGHT, -UI_CAMERA_COUNT_RIGHT, UI_CAMERA_TOP + 8);

    /* 设置当前页面按键处理回调 */
    set_current_page_handler(takephoto_key_handler);
    takephoto_register_up_callback(video_up_callback);
    takephoto_register_down_callback(vieo_key_down_callback);
    takephoto_register_menu_callback(key_takephoto_menu_callback);
    takephoto_register_mode_callback(key_takephoto_mode_callback);
    takephoto_register_ok_callback(key_takephoto_ok_callback);
    takephoto_register_power_callback(key_takephoto_power_callback);
    takephoto_register_long_menu_callback(video_long_menu_callback);
    // takephoto_register_long_mode_callback(video_long_mode_callback);
    takephoto_register_zoomin_callback(video_zoomin_key_cb);
    takephoto_register_zoomout_callback(video_zoomout_key_cb);
    takephoto_register_left_callback(video_left_callback);
    takephoto_register_right_callback(video_right_callback);
    //创建时间更新定时器
    if(date_timer_s == NULL) {
        date_timer_s = lv_timer_create(video_var_dynamic_update, 1000, ui);
    }
    // 立即执行一次更新
    lv_timer_ready(date_timer_s);
    lv_obj_update_layout(obj_vedio_s);
}

// 缩放按键事件处理
static void video_zoomin_key_cb(void)
{
    uint32_t new_level = get_zoom_level();
    // 设置放大比例
    MESSAGE_S Msg = {0};
    Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
    Msg.arg1      = 0;
    snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
    MODEMNG_SendMessage(&Msg);
    // 更新UI
    update_zoom_bar(new_level);
}

// 缩放按键事件处理
static void video_zoomout_key_cb(void)
{
    uint32_t new_level = get_zoom_level();
    // 设置放大比例
    MESSAGE_S Msg = {0};
    Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
    Msg.arg1      = 0;
    snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
    MODEMNG_SendMessage(&Msg);
    // 更新UI
    update_zoom_bar(new_level);
}

// left按键处理回调函数
static void video_left_callback(void)
{
    if(get_is_effect_exist() == true) {
        effect_Select_prev();
    }
}

// right按键处理回调函数
static void video_right_callback(void)
{
    if(get_is_effect_exist() == true) {
        effect_AISelect_next();
    }
}
