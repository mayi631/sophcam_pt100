/*
 * Copyright 2025 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */
#define DEBUG
#include "ui_layout.h"
#include "ui_image.h"
#include "common/extract_thumbnail.h"
#include "custom.h"
#include "events_init.h"
#include "filemng.h"
#include "gui_guider.h"
#include "image_process.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "page_all.h"
#include "rtt.h"
#include "wifi_check_dialog.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char current_image_path[256]; // 当前选中的图片路径，用于传递给图片查看页面
extern int g_total_media_files;
extern char **g_all_filenames;
extern int g_current_page;  // 当前页面索引
extern int g_total_pages;
extern int g_images_per_page;  // 每页显示的图片数量
extern int g_cam_id; // 当前相机ID，第一路
extern int g_album_image_index; // 当前焦点图片在所有图片中的索引

lv_obj_t *obj_AibumPic_s; //底层容器
static lv_obj_t *cont_top_s; // 顶部容器
static lv_obj_t *btn_delete_s; // 删除按钮
static lv_obj_t *btn_delete_label_s; // 删除按钮标签
static lv_obj_t *btn_back_s; // 返回按钮
static lv_obj_t *btn_back_label_s; // 返回按钮标签
static lv_obj_t *label_name_s; // 图片名称标签
static lv_obj_t *img_s; // 图片对象
static lv_timer_t *get_aiprocess_result_timer = NULL;  ;//获取结果定时器
static lv_timer_t *rtt_get_text_timer = NULL;  ;//获取结果定时器
static pthread_t aiprocess_thread = 0;                 //AI处理任务线程
static bool is_processing = false; // 限制重复点击AI处理
int g_return_page_index = 0;    // 返回时需要跳转的页面索引
int g_return_focus_index = 0;  // 返回时的焦点索引

lv_obj_t *label_processing;
lv_obj_t *spinner;
bool is_album_pic = false;
extern bool ai_custom_is_confire;

// 两次menu按键取消AI处理相关变量
#define MENU_CANCEL_PRESS_COUNT 2 // 连续按键次数
#define MENU_CANCEL_TOTAL_TIMEOUT 2000 // 总时间超时(ms)
#define MENU_CANCEL_INTERVAL_TIMEOUT 1000 // 相邻按键间隔超时(ms)

// 全局变量声明
//***********************AI处理取消*****************
static uint32_t menu_key_first_time = 0; // 第一次按键时间
static uint32_t menu_key_last_time = 0; // 上次按键时间
static uint8_t menu_key_press_count = 0; // 连续按键计数

static void ai_process_fast_cancel(void);

static void ai_process_select_event_handler(lv_event_t* e);
static void aiprocessing_ui_update(lv_timer_t* timer);
void album_process_ai_beauty(void);
static void ai_select_event_cb(lv_event_t *e);
void album_star_aiprocess(const int index);

static void ai_result_delete_after_cb(void);
static void ord_delete_after_cb(void);
static void rtt_get_text_timer_cb(lv_timer_t *timer);

static int set_current_photo_album_image_path(int index)
{
    MLOG_DBG("当前索引 %d\n", index);
    if(index < 0 || index >= g_total_media_files) {
        MLOG_ERR("索引 %d 超出范围 (0 - %d)\n", index, g_total_media_files - 1);
        return -1;
    }
    if(g_all_filenames[index] == NULL) {
        MLOG_ERR("索引 %d 处的文件名为空\n", index);
        return -1;
    }
    get_thumbnail_path(g_all_filenames[index], current_image_path, sizeof(current_image_path), PHOTO_LARGE_PATH);
    MLOG_DBG("设置当前图片路径: %s\n", current_image_path);
    return 0;
}

const char *get_curr_pic_path(void)
{
    return current_image_path;
}

// 资源释放函数
static void release_album_resources(lv_ui_t *ui)
{
    // 删除AI选择滚动容器
    delete_aiselect_scroll();
    destroy_voice_input_popup(); //ai语音自定义弹窗销毁

    takephoto_unregister_all_callback();
    // 删除获取结果定时器
    if (get_aiprocess_result_timer != NULL) {
        lv_timer_del(get_aiprocess_result_timer);
        get_aiprocess_result_timer = NULL;
    }

    // 销毁AI处理线程
    if(aiprocess_thread) {
        pthread_cancel(aiprocess_thread);
        pthread_join(aiprocess_thread, NULL);
        aiprocess_thread = 0;
    }
}

static void hide_label_timer_cb(lv_timer_t* timer)
{
    lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
    if(label != NULL && lv_obj_is_valid(label)) {
        lv_obj_del(label);
    }
    lv_timer_del(timer);
}

static void screen_PhotoAlbumPic_btn_delete_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));

    if(code == LV_EVENT_CLICKED) {
        if(is_processing) {
            lv_label_set_text(label_processing, str_language_processing_please_do_not_leave[get_curr_language()]);
            return;
        }

        // 获取当前AI处理结果路径
        char *result_path = process_result_get();
        normalize_path(result_path);
        MLOG_DBG("待删除的AI处理结果: %s\n", result_path);

        // 检查是否是AI处理结果
        int is_ai_result = (result_path != NULL && strlen(result_path) > 0);
        if(is_ai_result) {
            create_simple_delete_dialog(result_path);//创建确认浮窗
            sure_delete_register_callback(ai_result_delete_after_cb);
        } else {
            create_simple_delete_dialog(current_image_path); // 创建确认浮窗
            sure_delete_register_callback(ord_delete_after_cb);
        }
    }
}

static void screen_PhotoAlbumPic_btn_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            if(!is_processing) {
                release_album_resources(&g_ui);
                is_album_pic = false;
                // 保存返回时需要的信息
                g_return_page_index = g_album_image_index / g_images_per_page;
                g_return_focus_index = g_album_image_index % g_images_per_page;

                ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album_from_Pic, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                    false, true);
            } else {
                ai_process_fast_cancel();
            }
            break;
        }
        default: break;
    }
}

static void screen_ai_mode_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
    case LV_EVENT_CLICKED: {
        if (!is_processing) {
            release_album_resources(&g_ui);
            ui_load_scr_animation(&g_ui, &obj_Photo_AiMode_s, 1, NULL, photoMenu_AIMode, LV_SCR_LOAD_ANIM_NONE, 20,
                20, false, true);
        } else {
            lv_label_set_text(label_processing, str_language_processing_please_do_not_leave[get_curr_language()]);
        }
        break;
    }
        default: break;
    }
}

void events_init_screen_PhotoAlbumPic(lv_ui_t *ui)
{
    lv_obj_add_event_cb(btn_delete_s, screen_PhotoAlbumPic_btn_delete_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(btn_back_s, screen_PhotoAlbumPic_btn_back_event_handler, LV_EVENT_CLICKED, ui);
}

void photo_delete_anim_complete(lv_anim_t *a)
{
    // 非选择模式：跳转到图片查看页面
    lv_img_set_src(img_s, current_image_path);
    char *last_slash = strrchr(current_image_path, '/');
    // 如果找到了'/'，返回其后面的字符串；否则返回原字符串
    char *filename;
    if(last_slash != NULL) {
        filename = last_slash + 1;
    } else {
        filename = current_image_path; // 如果没有找到'/'，则使用原字符串
    }
    lv_label_set_text(label_name_s, filename);
    lv_anim_del(a, a->exec_cb);
}

static void albumpic_menu_callback(int key_code, int key_value)
{
    if(key_code == KEY_MENU && key_value == 1) {
        MLOG_DBG("albumpic_menu_callback\n");
        if(!is_processing) {
            release_album_resources(&g_ui);
            is_album_pic = false;
            ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album_from_Pic, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                true);
        } else {
            ai_process_fast_cancel();
        }
    } else if (key_code == KEY_PLAY) {
        if (key_value == 1) {
            if (AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                /* AI语音自定义模式：按AI按键开始语音输入（与拍照页面行为一致） */
                if (!is_valid_custom_input_popup()) {
                    lv_obj_t* scroll = get_aiselete_scroll_handl();
                    if (scroll != NULL) {
                        lv_obj_t* btn = lv_obj_get_child(scroll, 0);
                        if (btn != NULL) {
                            lv_obj_t* label = lv_obj_get_child(btn, 1);
                            if (label != NULL) {
                                lv_label_set_text(label, str_language_listening[get_curr_language()]);
                            }
                        }
                    }
                    /* 创建语音输入弹框 */
                    create_voice_input_popup();
                    /* 开始录音 */
                    rtt_start();
                    /* 启动定时器获取文本 */
                    if (rtt_get_text_timer == NULL) {
                        rtt_get_text_timer = lv_timer_create(rtt_get_text_timer_cb, 300, NULL);
                        lv_timer_ready(rtt_get_text_timer);
                    }
                }
            } else if (AIModeSelect_GetMode() == AI_NONE) {
                release_album_resources(&g_ui);
                ui_load_scr_animation(&g_ui, &obj_Photo_AiMode_s, 1, NULL, photoMenu_AIMode, LV_SCR_LOAD_ANIM_NONE, 20, 20,
                    false, true);
            } else if (AIModeSelect_GetMode() == AI_BEAUTY) {
                release_album_resources(&g_ui);
                ui_load_scr_animation(&g_ui, &obj_Photo_AiMode_s, 1, NULL, photoMenu_AIMode, LV_SCR_LOAD_ANIM_NONE, 20, 20,
                    false, true);
            } else {
                if (get_aiselete_scroll_handl() == NULL) {
                    if (AI_NONE != AIModeSelect_GetMode()) {
                        photoAISelect_listCreat(obj_AibumPic_s, ai_select_event_cb);
                    }
                } else {
                    delete_aiselect_scroll();
                }
            }

            /* 非AI_VOICE_CUSTOM模式，执行RTT反初始化（AI_VOICE_CUSTOM的rtt_init已在页面初始化时完成） */
            if (AIModeSelect_GetMode() != AI_VOICE_CUSTOM) {
                rtt_deinit();
            }
        } else if (key_value == 0) {
            if (AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                /* AI语音自定义模式：松开AI按键停止语音输入（与拍照页面行为一致） */
                lv_obj_t* scroll = get_aiselete_scroll_handl();
                if (scroll != NULL) {
                    lv_obj_t* btn = lv_obj_get_child(scroll, 0);
                    if (btn != NULL) {
                        lv_obj_t* label = lv_obj_get_child(btn, 1);
                        if (label != NULL) {
                            lv_label_set_text(label, str_language_hold_to_speak[get_curr_language()]);
                        }
                    }
                }
                /* 停止录音 */
                rtt_stop();
                voice_display_button();
            }
        }

    } else if ((key_code == KEY_LEFT || key_code == KEY_RIGHT) && key_value == 1 && is_processing == false) {
        if (get_aiselete_scroll_handl() != NULL && AIModeSelect_GetMode() != AI_NONE && !is_processing) {
            if (AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                // AI语音自定义模式：左右键都循环切换选择确定/取消
                voice_popup_toggle_select();
            } else if (key_code == KEY_RIGHT) {
                AISelect_next();
            } else {
                AISelect_prev();
            }
        } else {
            if(key_code == KEY_LEFT) // 左键
            {
                g_album_image_index++; // 左
                if(g_album_image_index >= g_total_media_files) {
                    g_album_image_index = g_total_media_files - 1;
                }
            } else if(key_code == KEY_RIGHT) {
                g_album_image_index--;
                if(g_album_image_index < 0) {
                    g_album_image_index = 0;
                }
            }
            g_return_page_index  = g_album_image_index / g_images_per_page; // 焦点
            g_return_focus_index = g_album_image_index % g_images_per_page; // 页面
            set_current_photo_album_image_path(g_album_image_index);
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_values(&anim, 0, 1);
            lv_anim_set_time(&anim, 50);
            lv_anim_set_path_cb(&anim, lv_anim_path_linear); // 使用线性路径
            lv_anim_set_completed_cb(&anim, photo_delete_anim_complete);
            lv_anim_start(&anim);
        }
    } else if ((key_code == KEY_OK) && key_value == 1) {
        if (get_aiselete_scroll_handl() != NULL && AIModeSelect_GetMode() != AI_NONE) {
            if (AIModeSelect_GetMode() != AI_VOICE_CUSTOM) {
                extern int g_current_selected_index;
                album_star_aiprocess(g_current_selected_index);
            }
            if (AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                voice_popup_custom_select_OK();
            }
        } else if (AIModeSelect_GetMode() == AI_BEAUTY) {
            album_process_ai_beauty();
        }
    } else if ((key_code == KEY_DOWN) && key_value == 1) {
        lv_obj_send_event(btn_delete_s, LV_EVENT_CLICKED, NULL);
    }
}

static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_GESTURE: {
            // 获取手势方向，需要 TP 驱动支持
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch(dir) {
                case LV_DIR_LEFT: {
                    lv_indev_wait_release(lv_indev_active());
                    g_album_image_index++; // 左滑
                    if(g_album_image_index >= g_total_media_files) {
                        g_album_image_index = g_total_media_files - 1;
                    }
                    set_current_photo_album_image_path(g_album_image_index);
                    break;
                }
                case LV_DIR_RIGHT: {
                    lv_indev_wait_release(lv_indev_active());
                    g_album_image_index--; // 右滑
                    if(g_album_image_index < 0) {
                        g_album_image_index = 0;
                    }
                    set_current_photo_album_image_path(g_album_image_index);
                }
                default: break;
            }
            break;
        }
        default: break;
    }
    g_return_page_index = g_album_image_index / g_images_per_page;
    g_return_focus_index = g_album_image_index % g_images_per_page;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_values(&anim, 0, 1);
    lv_anim_set_time(&anim, 50);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear); // 使用线性路径
    lv_anim_set_completed_cb(&anim, photo_delete_anim_complete);
    lv_anim_start(&anim);
}

// WiFi检查对话框返回回调 - 重新加载相册图片详情页
static void wifi_return_to_album_pic(void* user_data)
{
    lv_ui_t* ui = (lv_ui_t*)user_data;
    if (ui == NULL) {
        MLOG_ERR("UI is NULL, using default UI...\n");
        ui = &g_ui;
    }
    ui_load_scr_animation(ui, &obj_AibumPic_s, 1, NULL, setup_scr_screen_PhotoAlbumPic,
        LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

static void release_album_resources_wrapper(void)
{
    release_album_resources(&g_ui);
}

void setup_scr_screen_PhotoAlbumPic(lv_ui_t* ui)
{
    MLOG_DBG("loading page_PhotoAlbumPic...\n");
    is_album_pic = true;
    release_album_resources(ui);
    // 清除之前的AI处理结果路径，避免回放删除时错误删除上一张AI结果
    aiprocess_clean_cache();
    // Write codes screen_PhotoAlbumPic
    obj_AibumPic_s = lv_obj_create(NULL);
    lv_obj_set_size(obj_AibumPic_s, UI_X(640), UI_Y(480));
    lv_obj_set_scrollbar_mode(obj_AibumPic_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(obj_AibumPic_s, lv_color_hex(0),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj_AibumPic_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // 创建图片
    MLOG_DBG("current_image_path: %s\n", current_image_path);
    img_s = ui_image_create(obj_AibumPic_s);
    lv_obj_set_pos(img_s, 0, 0);
    lv_obj_set_size(img_s, UI_X(640), UI_Y(480));
    lv_img_set_src(img_s, current_image_path);

    // Write codes screen_PhotoAlbumPic_cont_top
    cont_top_s = lv_obj_create(obj_AibumPic_s);
    lv_obj_set_pos(cont_top_s, 0, 0);
    lv_obj_set_size(cont_top_s, UI_X(640), UI_Y(60));
    lv_obj_set_scrollbar_mode(cont_top_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(cont_top_s, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes screen_PhotoAlbumPic_btn_delete
    btn_delete_s = lv_button_create(obj_AibumPic_s);
    lv_obj_align(btn_delete_s, LV_ALIGN_BOTTOM_RIGHT, UI_X(-20), UI_Y(-8));
    lv_obj_set_size(btn_delete_s, UI_X(64), UI_Y(60));
    lv_obj_set_style_bg_opa(btn_delete_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    btn_delete_label_s = lv_label_create(btn_delete_s);
    lv_obj_set_style_text_color(btn_delete_label_s, lv_color_hex(0XFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(btn_delete_label_s, " " LV_SYMBOL_TRASH " ");
    lv_label_set_long_mode(btn_delete_label_s, LV_LABEL_LONG_WRAP);
    lv_obj_align(btn_delete_label_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(btn_delete_s, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(btn_delete_label_s, LV_PCT(100));
    lv_obj_set_style_text_font(btn_delete_label_s, &lv_font_montserrat_42, 0);

    // Write style for screen_PhotoAlbumPic_btn_delete, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(btn_delete_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_delete_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_delete_s, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_delete_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_delete_s, lv_color_hex(0x171717),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_delete_s, &lv_font_SourceHanSerifSC_Regular_30,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(btn_delete_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn_delete_s, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes screen_PhotoAlbumPic_btn_back
    btn_back_s = lv_button_create(cont_top_s);
    lv_obj_set_pos(btn_back_s, UI_X(4), 2);
    lv_obj_set_size(btn_back_s, UI_X(100), UI_Y(50));
    lv_obj_add_style(btn_back_s, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    btn_back_label_s = lv_label_create(btn_back_s);
    lv_label_set_text(btn_back_label_s, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(btn_back_label_s, LV_LABEL_LONG_WRAP);
    lv_obj_align(btn_back_label_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(btn_back_label_s, LV_PCT(100));
    lv_obj_add_style(btn_back_label_s, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    // AI按钮
    lv_obj_t* ai_btn = lv_imagebutton_create(cont_top_s);
    lv_obj_align(ai_btn, LV_ALIGN_TOP_LEFT, UI_X(110), 0);
    lv_obj_set_size(ai_btn, UI_X(58), UI_X(58));
    lv_obj_add_event_cb(ai_btn, ai_process_select_event_handler, LV_EVENT_CLICKED, NULL);
    const char* mode_icon[] = { "aimoshi.png", "风格_1.png", "AI背景替换_1.png", "美颜_1.png", "语音_自定义_1.png" };
    show_image(ai_btn, mode_icon[AIModeSelect_GetMode()]);

    lv_obj_t* text_mode_btn = lv_btn_create(cont_top_s);
    lv_obj_set_size(text_mode_btn, UI_X(58), UI_X(58)); // 与AI按钮相同大小
    lv_obj_align(text_mode_btn, LV_ALIGN_TOP_RIGHT, UI_X(-20), 0);
    // 设置与AI按钮相同的样式
    lv_obj_set_style_radius(text_mode_btn, lv_obj_get_style_radius(ai_btn, 0), 0);
    lv_obj_set_style_bg_color(text_mode_btn, lv_obj_get_style_bg_color(ai_btn, 0), 0);
    lv_obj_set_style_bg_opa(text_mode_btn, lv_obj_get_style_bg_opa(ai_btn, 0), 0);
    lv_obj_set_style_border_width(text_mode_btn, lv_obj_get_style_border_width(ai_btn, 0), 0);
    lv_obj_set_style_shadow_width(text_mode_btn, lv_obj_get_style_shadow_width(ai_btn, 0), 0);
    lv_obj_set_style_pad_all(text_mode_btn, 0, 0);
    lv_obj_add_event_cb(text_mode_btn, screen_ai_mode_btn_event_handler, LV_EVENT_CLICKED,NULL);

    // 创建标签并设置字体图标
    lv_obj_t *label = lv_label_create(text_mode_btn);
    if(label != NULL) {
        lv_label_set_text(label, LV_SYMBOL_LIST); // 使用LVGL内置的文本图标
        lv_obj_center(label);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_42, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0x1296db), 0);
    }
    // Write codes screen_PhotoAlbumPic_label_name
    char* last_slash = strrchr(current_image_path, '/');
    // 如果找到了'/'，返回其后面的字符串；否则返回原字符串
    char* filename;
    if (last_slash != NULL) {
        filename = last_slash + 1;
    } else {
        filename = current_image_path; // 如果没有找到'/'，则使用原字符串
    }
    label_name_s = lv_label_create(obj_AibumPic_s);
    lv_obj_set_pos(label_name_s, UI_X(189), UI_Y(20));
    lv_obj_set_size(label_name_s, UI_X(300), UI_Y(40));
    lv_label_set_text(label_name_s, filename);
    lv_label_set_long_mode(label_name_s, LV_LABEL_LONG_SCROLL);
    // Write style for screen_PhotoAlbumPic_label_name, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_name_s, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_name_s, get_usr_fonts(ALI_PUHUITI_FONTPATH, 20),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label_name_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label_name_s, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(label_name_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 设置当前页面的按键处理器
    set_current_page_handler(albumpic_menu_callback);
    // 创建处理中标签
    label_processing = lv_label_create(obj_AibumPic_s);
    if(label_processing == NULL) {
        MLOG_ERR("Failed to create processing label\n");
        return;
    }

    lv_label_set_text(label_processing, str_language_processing[get_curr_language()]);
    lv_obj_set_style_text_font(label_processing, get_usr_fonts(ALI_PUHUITI_FONTPATH, 24), 0);
    lv_obj_set_style_text_color(label_processing, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_processing, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(label_processing, LV_OBJ_FLAG_HIDDEN);

    // 创建进度指示器 - 兼容LVGL 9.3
    spinner = lv_spinner_create(obj_AibumPic_s); // 只有一个参数
    if(spinner) {
        lv_obj_set_size(spinner, UI_X(60), UI_X(60));
        lv_obj_set_style_arc_width(spinner, 6, 0); // 设置弧线宽度
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x0080FF), 0);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, UI_Y(60));
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    pthread_create(&aiprocess_thread, NULL, thread_ai_process_main, NULL);

    // Update current screen layout.
    lv_obj_update_layout(obj_AibumPic_s);

    // Init events for screen.
    events_init_screen_PhotoAlbumPic(ui);

    if(get_aiselete_scroll_handl() == NULL) {
        if(AI_NONE != AIModeSelect_GetMode()) {
            photoAISelect_listCreat(obj_AibumPic_s, ai_select_event_cb);
        }
    }

    if(AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
        rtt_init();
    } else {
        rtt_deinit();
    }

    /* 检查WiFi连接状态（仅在非AI_NONE模式时检查） */
    if (AIModeSelect_GetMode() != AI_NONE) {
        wifi_check_and_show_dialog(obj_AibumPic_s, wifi_return_to_album_pic, release_album_resources_wrapper, ui);
    }
}

void album_process_ai_beauty(void)
{
    set_defalt_retval();
    if(get_aiprocess_result_timer == NULL) {
        get_aiprocess_result_timer = lv_timer_create(aiprocessing_ui_update, 100, NULL);
        lv_timer_ready(get_aiprocess_result_timer);
    }
    if(!is_processing) {
        ai_process_state_set(AI_PROCESS_START);
        is_processing = true;
    } else {
        MLOG_DBG("重复点击处理，此次点击忽略");
    }
}

static void ai_select_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn        = lv_obj_get_child(get_aiselete_scroll_handl(), 0);
    lv_obj_t *label      = lv_obj_get_child(btn, 1);
    switch(code) {
        case LV_EVENT_PRESSED: // 获取语音自定义的输入语言
        {
            if(AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                lv_label_set_text(label, str_language_listening[get_curr_language()]);
                // 创建语音输入弹框
                create_voice_input_popup();
                // 开始录音
                rtt_start();
                // 启动定时器获取文本
                if(rtt_get_text_timer == NULL) {
                    rtt_get_text_timer = lv_timer_create(rtt_get_text_timer_cb, 300, NULL);
                    lv_timer_ready(rtt_get_text_timer);
                }
                return;
            }
        }; break;
        case LV_EVENT_CLICKED: {
            if(AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {//语言输入中
               return;
            }
            lv_obj_t *btn_clicked = lv_event_get_target(e);
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件，列表

            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
                if(btn_clicked == lv_obj_get_child(parent, i)) {
                    // 获取容器中的图片和标签对象
                    set_currIndex_focus(i); // 设置当前选择的style
                    album_star_aiprocess(i);
                }
            }
        }; break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_CANCEL://播放
        {
            if(AIModeSelect_GetMode() == AI_VOICE_CUSTOM) {
                // 停止录音
                rtt_stop();
                lv_label_set_text(label, str_language_hold_to_speak[get_curr_language()]);
                //  destroy_voice_input_popup();
                voice_display_button();
            }
        }; break;
        default: break;
    }
}

static void ai_process_select_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch(code) {
        case LV_EVENT_CLICKED: {

            if(AIModeSelect_GetMode() == AI_BEAUTY) {
                album_process_ai_beauty();
            } else {
                if(get_aiselete_scroll_handl() == NULL) {
                    if(AI_NONE != AIModeSelect_GetMode()) {
                        photoAISelect_listCreat(obj_AibumPic_s,ai_select_event_cb);
                    }
                } else {
                   delete_aiselect_scroll();
                }
            }

            /* 非AI_VOICE_CUSTOM模式，执行RTT反初始化（AI_VOICE_CUSTOM的rtt_init已在页面初始化时完成） */
            if (AIModeSelect_GetMode() != AI_VOICE_CUSTOM) {
                rtt_deinit();
            }
        }; break;
        default: break;
    }
}


static void aiprocessing_ui_update(lv_timer_t *timer)
{
    // 获取容器中的图片和标签对象
    if(lv_obj_has_flag(spinner, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if(lv_obj_has_flag(label_processing, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
    }

    static uint8_t tips_tim = 0; // 提示显示时间，提示时间超过2秒，则隐藏提示
    // MLOG_DBG("%s[%d]  %d tips_tim:%d\n",__func__,__LINE__,get_retval(),tips_tim);
    // 处理结果成功或失败
    if(get_retval() != 0 && get_retval() != DEFALT_RETVAL) {
        tips_tim++;
        lv_obj_set_style_text_color(label_processing, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (get_retval() == -2) {
            lv_label_set_text_fmt(label_processing, "%s %s",
                str_language_network_not_connected[get_curr_language()],
                str_language_please_try_again[get_curr_language()]);
        } else if (AIModeSelect_GetMode() == AI_BEAUTY && (get_retval() == -7 || get_retval() == -3)) {
            lv_label_set_text(label_processing, str_language_no_face_detected[get_curr_language()]);
        } else {
            lv_label_set_text_fmt(label_processing, str_language_processing_failed_error_code_retry_fmt[get_curr_language()], get_retval());
        }
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        // 清除处理结果路径
        is_processing = false;
        if (tips_tim >= 20) {
            lv_obj_add_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
            tips_tim = 0;
            aiprocess_clean_cache();
            if (get_aiprocess_result_timer != NULL) {
                lv_timer_del(get_aiprocess_result_timer);
                get_aiprocess_result_timer = NULL;
            }
        }
    } else if (get_retval() == 0) {
        char display_path[256];
        tips_tim = 0;
        snprintf(display_path, sizeof(display_path), "%s", process_result_get_thumbnail());
        lv_image_set_src(img_s, display_path);
        lv_obj_add_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        is_processing = false;
        if(get_aiprocess_result_timer != NULL) {
            lv_timer_del(get_aiprocess_result_timer);
            get_aiprocess_result_timer = NULL;
        }
    } else {
        tips_tim                  = 0; // 完成提示时间
        static uint8_t leave_tips = 0; // 离开提示时间
        const char *text          = lv_label_get_text(label_processing);
        lv_obj_set_style_text_color(label_processing, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (strcmp(text, str_language_processing_please_do_not_leave[get_curr_language()]) == 0) {
            leave_tips++;
            if (leave_tips >= 10) {
                lv_label_set_text(label_processing, str_language_processing[get_curr_language()]);
                leave_tips = 0;
            }
        } else {
            lv_label_set_text(label_processing, str_language_processing[get_curr_language()]);
        }
    }
}

void album_star_aiprocess(const int index)
{
    lv_obj_remove_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(img_s, current_image_path); // 每次点击，都需要先显示一下原图
    set_defalt_retval();
    extern const char *style_prompts[];
    extern const char* bg_prompts[];
    if (AIModeSelect_GetMode() == AI_SCENE_CHANGE) {
        MLOG_DBG("开始处理图像风格转换，提示词: %s\n", style_prompts[index]);
        aiprocess_set_prompt(style_prompts[index]);
    } else if (AIModeSelect_GetMode() == AI_BG_CHANGE) {
        MLOG_DBG("开始处理图像背景替换，提示词: %s\n", bg_prompts[index]);
        aiprocess_set_prompt(bg_prompts[index]);
    }
    if (get_aiprocess_result_timer == NULL) {
        get_aiprocess_result_timer = lv_timer_create(aiprocessing_ui_update, 100, NULL);
        lv_timer_ready(get_aiprocess_result_timer);
    }
    if(!is_processing) {
        ai_process_state_set(AI_PROCESS_START);
        is_processing = true;
    } else {
        MLOG_DBG("重复点击处理，此次点击忽略");
    }
}

static void album_start_ai_custom_process(const char* text)
{
    if(text == NULL) {
        return;
    }
    lv_obj_remove_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(img_s, current_image_path); // 每次点击，都需要先显示一下原图
    set_defalt_retval();

    if ((AIModeSelect_GetMode() == AI_VOICE_CUSTOM)) {
        MLOG_DBG("自定义提示词: %s\n", text);
        aiprocess_set_prompt(text);
    }
    if (get_aiprocess_result_timer == NULL) {
        get_aiprocess_result_timer = lv_timer_create(aiprocessing_ui_update, 100, NULL);
        lv_timer_ready(get_aiprocess_result_timer);
    }
    if(!is_processing) {
        ai_process_state_set(AI_PROCESS_START);
        is_processing = true;
    } else {
        MLOG_DBG("重复点击处理，此次点击忽略");
    }
}

static void ai_result_delete_after_cb(void)
{
    // 清除处理结果路径
    aiprocess_clean_cache();
    lv_image_set_src(img_s, current_image_path);
}

static void ord_delete_after_cb(void)
{
    char *last_slash = strrchr(current_image_path, '/');
    char *filename;
    if(last_slash != NULL) {
        filename = last_slash + 1;
    } else {
        filename = current_image_path;
    }
    int j;
    for(j = 0; j < g_total_media_files; j++) {
        if(g_all_filenames[j] != NULL && strcmp(g_all_filenames[j], filename) == 0) {
            free(g_all_filenames[j]);
            g_all_filenames[j] = NULL;
            // 将后面的文件名前移
            int k;
            for(k = j; k < g_total_media_files - 1; k++) {
                g_all_filenames[k] = g_all_filenames[k + 1];
            }
            if(((j + 1) % g_images_per_page == 1) && (j == g_total_media_files - 1)) {
                g_current_page--;
                g_total_pages--;
                if(g_current_page < 0) {
                    g_current_page = 0;
                }
            }
            g_total_media_files--;
            // 调整返回页面和焦点索引
            if(j <= g_album_image_index) {
                // 如果删除的是当前或之前的图片，需要调整索引
                g_album_image_index--;
                if(g_album_image_index < 0) g_album_image_index = 0;

                // 重新计算返回页面和焦点
                g_return_page_index  = g_album_image_index / g_images_per_page;
                g_return_focus_index = g_album_image_index % g_images_per_page;
            }

            break;
        }
    }

    release_album_resources(&g_ui);
    is_album_pic = 0;
    ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album_from_Pic, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

static void rtt_get_text_timer_cb(lv_timer_t *timer)
{
    char* custom_voice_text = NULL;
    int32_t ret = 0;
    ret = rtt_get_text(&custom_voice_text);
    if(ret != RTT_SUCCESS) {
        MLOG_INFO("text: %s\n", custom_voice_text);
        if(rtt_get_text_timer) {
            lv_timer_del(rtt_get_text_timer);
            rtt_get_text_timer = NULL;
        }
        rtt_reset();
        return;
    }

    if(custom_voice_text != NULL) {
        MLOG_INFO("text: %s\n", custom_voice_text);
        if(strlen(custom_voice_text) > 0) {
            voice_text_set(custom_voice_text);
        }
    }

    if((rtt_is_finial()) && ai_custom_is_confire) {
        if(rtt_get_text_timer) {
            lv_timer_del(rtt_get_text_timer);
            rtt_get_text_timer = NULL;
        }
        album_start_ai_custom_process(custom_voice_text);
    } else {
        lv_timer_reset(timer);
    }
}

// 取消AI处理处理函数
static void cancel_ai_process_handler(void)
{
    MLOG_DBG("取消AI处理\n");

    // 标记处理结束（必须在调用取消函数之前）
    is_processing = false;

    // 调用image_process中的取消函数
    ai_process_cancel_and_reset();

    // 删除获取结果定时器
    if (get_aiprocess_result_timer != NULL) {
        lv_timer_del(get_aiprocess_result_timer);
        get_aiprocess_result_timer = NULL;
    }
    // 恢复原来的图片显示（原始图片）
    lv_image_set_src(img_s, current_image_path);

    lv_obj_add_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
    if (spinner != NULL) {
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }

    MLOG_DBG("AI处理已取消，状态已重置\n");
}

static void ai_process_fast_cancel(void)
{
    // 正在AI处理中，使用连续按键计数方式取消
    uint32_t current_time = lv_tick_get();

    // 如果是第一次按下，记录起始时间
    if (menu_key_press_count == 0) {
        menu_key_first_time = current_time;
        menu_key_last_time = current_time;
        menu_key_press_count = 1;
        MLOG_DBG("开始连续按键计数，当前第1次\n");
        // 更新提示文字
        lv_obj_clear_flag(label_processing, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_processing, str_language_press_again_cancel_processing[get_curr_language()]);
        lv_obj_set_style_text_color(label_processing, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        // 检查总时间是否超过超时时间
        if (current_time - menu_key_first_time > MENU_CANCEL_TOTAL_TIMEOUT) {
            // 超过总时间，重新开始计数
            menu_key_press_count = 1;
            menu_key_first_time = current_time;
            menu_key_last_time = current_time;
            MLOG_DBG("总时间超过%dms，重新开始计数，当前第1次\n", MENU_CANCEL_TOTAL_TIMEOUT);
            // 更新提示文字
            lv_label_set_text(label_processing, str_language_press_again_cancel_processing[get_curr_language()]);
            return;
        }
        // 检查相邻按键间隔是否超过超时时间
        if (current_time - menu_key_last_time > MENU_CANCEL_INTERVAL_TIMEOUT) {
            // 相邻按键间隔超过超时时间，重新开始计数
            menu_key_press_count = 1;
            menu_key_first_time = current_time;
            menu_key_last_time = current_time;
            MLOG_DBG("按键间隔超过%dms，重新开始计数，当前第1次\n", MENU_CANCEL_INTERVAL_TIMEOUT);
            // 更新提示文字
            lv_label_set_text(label_processing, str_language_press_again_cancel_processing[get_curr_language()]);
        } else {
            // 符合条件，计数增加
            menu_key_press_count++;
            menu_key_last_time = current_time;
            MLOG_DBG("连续按键按下 %d 次，总时间: %dms，上次间隔: %dms\n",
                menu_key_press_count, current_time - menu_key_first_time, current_time - menu_key_last_time);
        }
    }

    // 检查是否达到连续按键次数
    if (menu_key_press_count >= MENU_CANCEL_PRESS_COUNT) {
        MLOG_DBG("连续按下%d次MENU按键，取消AI处理\n", MENU_CANCEL_PRESS_COUNT);
        // 重置计数器
        menu_key_press_count = 0;
        menu_key_first_time = 0;
        menu_key_last_time = 0;
        // 取消AI处理
        cancel_ai_process_handler();
    }
}
