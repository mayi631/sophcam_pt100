#define DEBUG
/*********************
 *      INCLUDES
 *********************/
#include "common/extract_thumbnail.h"
#include "custom.h"
#include "filemng.h"
#include "lvgl.h"
#include "lvgl/src/libs/freetype/lv_freetype.h"
#include "mapi_ao.h"
#include "page_all.h"
#include "ui_common.h"
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// 语音输入弹框相关变量
static lv_obj_t *voice_input_popup = NULL;
static lv_obj_t *voice_text_label = NULL;
static lv_timer_t *voice_popup_timer = NULL;
extern lv_style_t ttf_font_22;
static lv_obj_t* ai_voice_confirm_btn = NULL;
static lv_obj_t* ai_voice_cancel_btn = NULL;
bool ai_custom_is_confire = false; //AI自定义是否按键确定
static bool is_start_ai_process = false; //选择确定之后正式开始处理
// 语音弹窗当前是否选中确认按钮（true=确认，false=取消）
static bool voice_popup_selected_confirm = false; //循环选择

void destroy_voice_input_popup(void)
{
    if (voice_input_popup) {
        lv_obj_del(ai_voice_confirm_btn);
        ai_voice_confirm_btn = NULL;
        lv_obj_del(ai_voice_cancel_btn);
        ai_voice_cancel_btn = NULL;
        lv_obj_del(voice_input_popup);
        voice_input_popup = NULL;
        voice_text_label = NULL;
    }

    if (voice_popup_timer) {
        lv_timer_del(voice_popup_timer);
        voice_popup_timer = NULL;
    }
}

static void confirm_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        destroy_voice_input_popup();
        ai_custom_is_confire = true;
    }
}

static void cancel_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        destroy_voice_input_popup();
        ai_custom_is_confire = false;
        is_start_ai_process = false;
    }
}

// 创建语音输入弹框
void create_voice_input_popup(void)
{
    ai_custom_is_confire = false;
    is_start_ai_process = false;

    voice_input_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(voice_input_popup, H_RES, V_RES);
    lv_obj_set_scrollbar_mode(voice_input_popup, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(voice_input_popup, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(voice_input_popup, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(voice_input_popup, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(voice_input_popup, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(voice_input_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(voice_input_popup, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // 创建弹框容器
    lv_obj_t *voice_input_popup_dispaly = lv_obj_create(voice_input_popup);
    lv_obj_set_size(voice_input_popup_dispaly, 400, 120);
    lv_obj_align(voice_input_popup_dispaly, LV_ALIGN_TOP_MID, 0, 100);

    // 现代化样式
    lv_obj_set_style_bg_color(voice_input_popup_dispaly, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(voice_input_popup_dispaly, LV_OPA_90, 0);
    lv_obj_set_style_radius(voice_input_popup_dispaly, 16, 0);
    lv_obj_set_style_border_width(voice_input_popup_dispaly, 0, 0);
    lv_obj_set_style_shadow_width(voice_input_popup_dispaly, 50, 0);
    lv_obj_set_style_shadow_opa(voice_input_popup_dispaly, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(voice_input_popup_dispaly, 20, 0);
    lv_obj_set_style_text_align(voice_input_popup_dispaly, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 语音文本
    voice_text_label = lv_label_create(voice_input_popup_dispaly);
    lv_label_set_text(voice_text_label, str_language_describe_style_creation[get_curr_language()]);
    lv_obj_set_style_text_color(voice_text_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(voice_text_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 20), 0);
    lv_obj_align(voice_text_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_size(voice_text_label, 360,80);

    // 确认按钮
    ai_voice_confirm_btn = lv_btn_create(voice_input_popup);
    lv_obj_set_size(ai_voice_confirm_btn, 120, 42);
    lv_obj_align(ai_voice_confirm_btn, LV_ALIGN_BOTTOM_LEFT, 120, -140);
    lv_obj_set_style_bg_color(ai_voice_confirm_btn, lv_color_hex(0x007AFF), 0);
    lv_obj_set_style_pad_all(ai_voice_confirm_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ai_voice_confirm_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *confirm_label = lv_label_create(ai_voice_confirm_btn);
    lv_label_set_text(confirm_label, str_language_confirm[get_curr_language()]);
    lv_obj_add_style(confirm_label, &ttf_font_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(ai_voice_confirm_btn, confirm_cb, LV_EVENT_CLICKED, NULL);

    // 取消按钮
    ai_voice_cancel_btn = lv_btn_create(voice_input_popup);
    lv_obj_set_size(ai_voice_cancel_btn, 120, 42);
    lv_obj_align(ai_voice_cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -120, -140);
    lv_obj_set_style_bg_color(ai_voice_cancel_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(ai_voice_cancel_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(ai_voice_cancel_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *cancel_label = lv_label_create(ai_voice_cancel_btn);
    lv_label_set_text(cancel_label, str_language_cancel[get_curr_language()]);
    lv_obj_add_style(cancel_label, &ttf_font_22, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(ai_voice_cancel_btn, cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_border_color(ai_voice_cancel_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(ai_voice_cancel_btn, 2, LV_PART_MAIN);
}

void voice_display_button(void)
{
    if (lv_obj_is_valid(ai_voice_confirm_btn) && lv_obj_has_flag(ai_voice_confirm_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(ai_voice_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_is_valid(ai_voice_cancel_btn) && lv_obj_has_flag(ai_voice_cancel_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(ai_voice_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }
    // 默认选中取消按钮
    voice_popup_custom_select_cancel();
}

void voice_hide_button(void)
{
    if (lv_obj_is_valid(ai_voice_confirm_btn) && !lv_obj_has_flag(ai_voice_confirm_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(ai_voice_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_is_valid(ai_voice_cancel_btn) && !lv_obj_has_flag(ai_voice_cancel_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(ai_voice_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void voice_text_set(char *text)
{
    if (voice_text_label != NULL) {
        lv_label_set_text(voice_text_label, text);
    }
}

bool is_valid_custom_input_popup(void)
{
    return lv_obj_is_valid(voice_input_popup);
}

void voice_popup_custom_select_cancel(void)
{
    if (!lv_obj_is_valid(voice_input_popup)) {
        return;
    }
    //获取确定和取消的句柄
    lv_obj_t* btn_cancel = lv_obj_get_child(voice_input_popup, 2);
    if (btn_cancel == NULL)
        return;
    lv_obj_t* btn_ok = lv_obj_get_child(voice_input_popup, 1);
    if (btn_ok == NULL)
        return;
    is_start_ai_process = false;
    voice_popup_selected_confirm = false;
    // 取消按钮高亮效果 - 蓝色边框+蓝色背景
    lv_obj_set_style_border_color(btn_cancel, lv_color_hex(0x007AFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_cancel, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x007AFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_40, LV_PART_MAIN);

    // 确认按钮恢复默认样式
    lv_obj_set_style_border_width(btn_ok, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_ok, LV_OPA_COVER, LV_PART_MAIN);
}

void voice_popup_custom_select_confirm(void)
{
    if (!lv_obj_is_valid(voice_input_popup)) {
        return;
    }
    //获取确定和取消的句柄
    lv_obj_t* btn_cancel = lv_obj_get_child(voice_input_popup, 2);
    if (btn_cancel == NULL)
        return;
    lv_obj_t* btn_ok = lv_obj_get_child(voice_input_popup, 1);
    if (btn_ok == NULL)
        return;
    is_start_ai_process = true;
    voice_popup_selected_confirm = true;
    // 取消按钮恢复默认样式
    lv_obj_set_style_border_width(btn_cancel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_COVER, LV_PART_MAIN);

    // 确认按钮高亮效果 - 蓝色边框+蓝色背景
    lv_obj_set_style_border_color(btn_ok, lv_color_hex(0x007AFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_ok, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x007AFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_ok, LV_OPA_40, LV_PART_MAIN);
}

// 语音弹窗循环切换选择（每次调用切换确定/取消状态）
void voice_popup_toggle_select(void)
{
    if (!lv_obj_is_valid(voice_input_popup)) {
        return;
    }
    // 根据当前状态切换选择
    if (voice_popup_selected_confirm) {
        voice_popup_custom_select_cancel();
    } else {
        voice_popup_custom_select_confirm();
    }
}

// 获取当前选择状态（true=确认，false=取消）
bool voice_popup_get_selected_confirm(void)
{
    return voice_popup_selected_confirm;
}

void voice_popup_custom_select_OK(void)
{
    if (!lv_obj_is_valid(voice_input_popup)) {
        return;
    }
    //获取确定和取消的句柄
    lv_obj_t* btn_cancel = lv_obj_get_child(voice_input_popup, 2);
    if (btn_cancel == NULL)
        return;
    lv_obj_t* btn_ok = lv_obj_get_child(voice_input_popup, 1);
    if (btn_ok == NULL)
        return;
    if (is_start_ai_process == true)
        ai_custom_is_confire = true;
    else
        ai_custom_is_confire = false;

    if (ai_custom_is_confire == false) {
        lv_obj_send_event(btn_cancel, LV_EVENT_CLICKED, NULL);
    } else if (ai_custom_is_confire == true) {
        lv_obj_send_event(btn_ok, LV_EVENT_CLICKED, NULL);
    }
}
