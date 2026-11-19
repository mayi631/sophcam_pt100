/*
 * Copyright 2025 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */
#define DEBUG
#include "common/takephoto.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "lvgl.h"
#include "page_all.h"
#include "ui_common.h"
#include <stdio.h>
#include <stdlib.h>

lv_obj_t* obj_home_s;

extern lv_style_t ttf_font_24;
static uint8_t curr_focus_index = 0;

// 2D网格导航配置
#define GRID_COLS 3  // 每行3个按钮
#define GRID_ROWS 2  // 共2行
#define GRID_MAX_OBJECTS GRID_COLS * GRID_ROWS  // 最大对象数量

// 添加焦点组和当前焦点索引
static lv_obj_t *focusable_objects[GRID_MAX_OBJECTS] = {NULL}; // 存储可聚焦的对象

static bool is_first_init = true; // 是否是第一次初始化

// 自定义点击回调函数
static void home_click_callback(lv_obj_t *obj)
{
    // 根据对象在数组中的位置确定操作
    int obj_index = -1;
    for (int i = 0; i < GRID_MAX_OBJECTS; i++) {
        if (focusable_objects[i] == obj) {
            obj_index = i;
            curr_focus_index = i;
            break;
        }
    }

    if (is_first_init) {
        is_first_init = false;
        //显示黑色背景色，盖掉开机logo。后续送流的时候，会盖掉背景色。
        system("devmem 0x0a094094 32 0x00010028");
    }
    if (obj_index >= 0) {
        switch (obj_index) {
        case 0: // 拍照
        {
            usleep(150000); // 延时100ms，确保按键音效播放完毕。
            ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
            MESSAGE_S Msg = { 0 };
            Msg.topic = EVENT_MODEMNG_MODESWITCH;
            Msg.arg1 = WORK_MODE_PHOTO;
            MODEMNG_SendMessage(&Msg);
            ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
        } break;
        case 1: // AI拍万物
        {
            usleep(150000); // 延时100ms，确保按键音效播放完毕。
            MESSAGE_S Msg = { 0 };
            Msg.topic = EVENT_MODEMNG_MODESWITCH;
            Msg.arg1 = WORK_MODE_PHOTO;
            MODEMNG_SendMessage(&Msg);
            ui_load_scr_animation(&g_ui, &page_ai_camera_s, 1, NULL, create_ai_camera_screen, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
        } break;
            case 2: // 相册
            {
                MESSAGE_S Msg = {0};
                Msg.topic     = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1      = WORK_MODE_PLAYBACK;
                MODEMNG_SendMessage(&Msg);
                ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
            } break;
            case 3: // AI对话
            {
                ui_load_scr_animation(&g_ui, &page_ai_talk_s, 1,NULL, create_chat_screen, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
            } break;
            case 4: // 拍译
            {
                usleep(150000); // 延时100ms，确保按键音效播放完毕。
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PHOTO;
                MODEMNG_SendMessage(&Msg);
                ui_load_scr_animation(&g_ui, &page_ai_shoot_translation_s, 1,NULL, create_ai_tranlation, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
            } break;
            case 5: // 设置
            {
                ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
            } break;
            }
    }
}

static void home_All_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch(code) {
        case LV_EVENT_CLICKED: {
            lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
                if(btn_clicked == lv_obj_get_child(parent, i)) {
                    if (is_first_init) {
                        is_first_init = false;
                        //显示黑色背景色，盖掉开机logo。后续送流的时候，会盖掉背景色。
                        system("devmem 0x0a094094 32 0x00010028");
                    }
                    curr_focus_index = i;
                    lv_obj_t* target_obj = lv_obj_get_child(obj_home_s, curr_focus_index);
                    lv_obj_add_state(target_obj, LV_STATE_FOCUS_KEY);
                    set_focus_index(curr_focus_index);
                    if (get_exit_completed() != true)
                        return; //如果没有退出完成，舍弃这次点击或按键
                    switch (i) // photoSettings_cont_s 窗口多了横线控件.每两个是一个btn
                    {
                    case 0: // 拍照
                    {
                        usleep(150000); // 延时100ms，确保按键音效播放完毕。
                        MESSAGE_S Msg = { 0 };
                        Msg.topic = EVENT_MODEMNG_MODESWITCH;
                        Msg.arg1 = WORK_MODE_PHOTO;
                        MODEMNG_SendMessage(&Msg);
                        enable_focus();
                        ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                    } break;
                    case 1: // AI拍万物
                    {
                        usleep(150000); // 延时100ms，确保按键音效播放完毕。
                        MESSAGE_S Msg = { 0 };
                        Msg.topic = EVENT_MODEMNG_MODESWITCH;
                        Msg.arg1 = WORK_MODE_PHOTO;
                        MODEMNG_SendMessage(&Msg);
                        enable_focus();
                        ui_load_scr_animation(&g_ui, &page_ai_camera_s, 1, NULL, create_ai_camera_screen, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                    } break;
                    case 2: // 相册
                    {
                        MESSAGE_S Msg = { 0 };
                        Msg.topic = EVENT_MODEMNG_MODESWITCH;
                        Msg.arg1 = WORK_MODE_PLAYBACK;
                        MODEMNG_SendMessage(&Msg);
                        ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                    } break;
                        case 3: // AI对话
                        {
                            ui_load_scr_animation(&g_ui, &page_ai_talk_s, 1,NULL, create_chat_screen, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                        } break;
                        case 4: // 拍译
                        {
                            usleep(150000); // 延时100ms，确保按键音效播放完毕。
                            MESSAGE_S Msg = { 0 };
                            Msg.topic = EVENT_MODEMNG_MODESWITCH;
                            Msg.arg1 = WORK_MODE_PHOTO;
                            MODEMNG_SendMessage(&Msg);
                            enable_focus();
                            ui_load_scr_animation(&g_ui, &page_ai_shoot_translation_s, 1, NULL, create_ai_tranlation, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                        } break;
                        case 5: // 设置
                        {
                            ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                        } break;
                        }
                }
            }
            break;
        }
        default: break;
        }
}

void setup_scr_home1(lv_ui_t* ui)
{
    MLOG_DBG("loading page_home1...\n");
    extern uint8_t g_last_scr_mode;
    g_last_scr_mode = 0;

    obj_home_s = lv_obj_create(NULL);
    lv_obj_set_size(obj_home_s, H_RES, V_RES);

    MLOG_DBG("obj_home_s lv_obj_get_scrollbar_mode: %d\n", lv_obj_get_scrollbar_mode(obj_home_s));
    lv_obj_set_scrollbar_mode(obj_home_s, LV_SCROLLBAR_MODE_OFF); // 禁用滚动条，默认 LV_SCROLLBAR_MODE_AUTO
    lv_obj_set_style_bg_opa(obj_home_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT); // 不透明度 0~255，255 完全不透明
    lv_obj_set_style_bg_color(obj_home_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT); // 背景颜色
    lv_obj_set_style_bg_grad_dir(obj_home_s, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);    // 无渐变色
    lv_obj_set_style_bg_image_src(obj_home_s,&Desktop,LV_PART_MAIN | LV_STATE_DEFAULT);

    // 1. 拍照
    lv_obj_t *btn_take_photo = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_take_photo, 170, 200);
    lv_obj_align(btn_take_photo, LV_ALIGN_TOP_LEFT, 50, 36);
    lv_obj_set_style_bg_opa(btn_take_photo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_take_photo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_take_photo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_take_photo, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_take_photo, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_take_photo, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_take_photo, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn_take_photo, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_take_photo, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_take_photo, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);
    show_image(btn_take_photo, "AI拍照_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_take_photo, "S:/AI拍照.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片

    lv_obj_t* label_take_photo = lv_label_create(btn_take_photo);
    lv_label_set_text(label_take_photo, str_language_ai_camera[get_curr_language()]);
    lv_obj_align(label_take_photo, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_take_photo, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_take_photo, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_take_photo, LV_PCT(100), 30);
    lv_label_set_long_mode(label_take_photo, LV_LABEL_LONG_SCROLL);

    // 主页面上增加按钮
    // 1. 增加AI拍万物
    lv_obj_t *btn_iden_things = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_iden_things, 170, 200);
    lv_obj_align(btn_iden_things, LV_ALIGN_TOP_LEFT, 240, 36);
    lv_obj_set_style_bg_opa(btn_iden_things, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_iden_things, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn_iden_things, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_iden_things, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_iden_things, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_iden_things, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_iden_things, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_iden_things, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn_iden_things, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_iden_things, 0, LV_STATE_DEFAULT);
    show_image(btn_iden_things, "AI拍万物_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_iden_things, "S:/AI拍万物.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片
    lv_obj_add_event_cb(btn_iden_things, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);

    lv_obj_t* label_iden_things = lv_label_create(btn_iden_things);
    lv_label_set_text(label_iden_things, str_language_ai_recognizes_everything[get_curr_language()]);
    lv_obj_align(label_iden_things, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_iden_things, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_iden_things, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_iden_things, LV_PCT(100), 30);
    lv_label_set_long_mode(label_iden_things, LV_LABEL_LONG_SCROLL);

    // 1. 相册
    lv_obj_t *btn_album = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_album, 150, 200);
    lv_obj_align(btn_album, LV_ALIGN_TOP_LEFT, 430, 36);
    lv_obj_set_style_bg_opa(btn_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_album, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_album, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_album, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn_album, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_album, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_album, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);
    show_image(btn_album, "相册_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_album, "S:/相册.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片

    lv_obj_t* label_album = lv_label_create(btn_album);
    lv_label_set_text(label_album, str_language_album[get_curr_language()]);
    lv_obj_align(label_album, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_album, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_album, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_album, LV_PCT(100), 30);
    lv_label_set_long_mode(label_album, LV_LABEL_LONG_SCROLL);

    // 1. ai对话
    lv_obj_t *btn_aitalk = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_aitalk, 170, 200);
    lv_obj_align(btn_aitalk, LV_ALIGN_TOP_LEFT, 50, 250);
    lv_obj_set_style_bg_opa(btn_aitalk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_aitalk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_aitalk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_aitalk, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_aitalk, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_aitalk, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_aitalk, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_aitalk, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);
    lv_obj_set_style_pad_all(btn_aitalk, 0, LV_STATE_DEFAULT);
    show_image(btn_aitalk, "AI对话_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_aitalk, "S:/AI对话.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片

    lv_obj_t* label_aitalk = lv_label_create(btn_aitalk);
    lv_label_set_text(label_aitalk, str_language_ai_chat[get_curr_language()]);
    lv_obj_align(label_aitalk, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_aitalk, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_aitalk, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_aitalk, LV_PCT(100), 30);
    lv_label_set_long_mode(label_aitalk, LV_LABEL_LONG_SCROLL);

    // 1. 拍照翻译
    lv_obj_t *btn_translation = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_translation, 170, 200);
    lv_obj_align(btn_translation, LV_ALIGN_TOP_LEFT, 240, 250);
    lv_obj_set_style_bg_opa(btn_translation, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_translation, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_translation, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_translation, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_translation, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_translation, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_translation, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_align(btn_translation, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_translation, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_translation, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);
    show_image(btn_translation, "拍译_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_translation, "S:/拍译.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片

    lv_obj_t* label_translation = lv_label_create(btn_translation);
    lv_label_set_text(label_translation, str_language_photo_translation[get_curr_language()]);
    lv_obj_align(label_translation, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_translation, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_translation, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_translation, LV_PCT(100), 30);
    lv_label_set_long_mode(label_translation, LV_LABEL_LONG_SCROLL);

    // 1. 设置
    lv_obj_t *btn_setting = lv_btn_create(obj_home_s);
    lv_obj_set_size(btn_setting, 170, 200);
    lv_obj_align(btn_setting, LV_ALIGN_TOP_LEFT, 430, 250);
    lv_obj_set_style_bg_opa(btn_setting, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_setting, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_setting, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_setting, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_setting, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_setting, &lv_font_montserratMedium_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_setting, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_align(btn_setting, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_setting, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_setting, home_All_btn_event_handler, LV_EVENT_CLICKED, (void*)0);
    show_image(btn_setting, "设置_未选.png"); // 非焦点状态图片
    lv_obj_set_style_bg_image_src(btn_setting, "S:/设置.png", LV_PART_MAIN | LV_STATE_FOCUS_KEY); // 焦点状态图片

    lv_obj_t* label_setting = lv_label_create(btn_setting);
    lv_label_set_text(label_setting, str_language_settings[get_curr_language()]);
    lv_obj_align(label_setting, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_style(label_setting, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_setting, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_size(label_setting, LV_PCT(100), 30);
    lv_label_set_long_mode(label_setting, LV_LABEL_LONG_SCROLL);

    lv_obj_t* target_obj = lv_obj_get_child(obj_home_s, curr_focus_index);
    // 初始化焦点组
    init_focus_group(obj_home_s, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS, home_click_callback, target_obj);
    lv_obj_add_state(target_obj, LV_STATE_FOCUS_KEY);
    // 设置当前页面的按键处理器
    set_current_page_handler(handle_grid_navigation);
}
