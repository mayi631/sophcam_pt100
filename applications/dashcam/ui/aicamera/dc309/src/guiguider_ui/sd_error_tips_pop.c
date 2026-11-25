// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#define DEBUG
#include "ui_layout.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "page_all.h"
#include "style_common.h"
#include "ui_common.h"
#include <stdio.h>

// #endregion
// #############################################################################
// ! #region 2. 数据结构定义
// #############################################################################
// #endregion
// #############################################################################
// ! #region 3. 全局变量 &  函数声明
// #############################################################################
// 全局变量
static lv_obj_t* sd_error_mask = NULL;
static lv_obj_t* sd_error_popup = NULL;
extern lv_style_t ttf_font_28;
// SD卡异常提醒相关函数声明
void show_sd_error_warning(void);
static void cancel_close_sd_error_popup_event(lv_event_t* e);
static void confir_sd_error_format_handler(lv_event_t* e);
static void create_sd_error_warning_popup(void);
// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################
static void delete_sd_error_warning(void)
{
    if (lv_obj_is_valid(sd_error_popup)) {
        lv_obj_del(sd_error_popup);
        sd_error_popup = NULL;
    }
    if (lv_obj_is_valid(sd_error_mask)) {
        lv_obj_del(sd_error_mask);
        sd_error_mask = NULL;
    }
}

static void sd_err_format_display_tips_anim_complete(lv_anim_t* a)
{
    /* 动画完成回调：删除浮窗并停止动画 */
    delete_sd_error_warning();
    lv_anim_del(a, a->exec_cb);
}

static void confir_format_ingb(void)
{
    for (uint8_t j = 0; j < lv_obj_get_child_cnt(sd_error_popup); j++) {
        if (!lv_obj_has_flag(lv_obj_get_child(sd_error_popup, j), LV_OBJ_FLAG_HIDDEN) && j != 2) {
            lv_obj_add_flag(lv_obj_get_child(sd_error_popup, j), LV_OBJ_FLAG_HIDDEN);
        }
    }

    MLOG_DBG("正在格式化......\n");
    // 先显示"正在格式化"并刷新UI
    lv_label_set_text(lv_obj_get_child(sd_error_popup, 2), str_language_formatting[get_curr_language()]);
    lv_obj_add_style(lv_obj_get_child(sd_error_popup, 2), &ttf_font_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lv_obj_get_child(sd_error_popup, 2), LV_ALIGN_TOP_MID, 0, 0);
    lv_refr_now(lv_disp_get_default());
    MESSAGE_S Msg = { 0 };
    Msg.topic = EVENT_MODEMNG_CARD_FORMAT;
    // 发送同步消息，通过 ui_event_type 判断结果
    UICOMM_SendSyncMsg(&Msg, 20000); // 20秒超时
    if (ui_event_type == EVT_FORMAT_SUCCESS) {
        lv_label_set_text(lv_obj_get_child(sd_error_popup, 2), str_language_format_successful[get_curr_language()]);
    } else {
        lv_label_set_text(lv_obj_get_child(sd_error_popup, 2), str_language_format_failed[get_curr_language()]);
    }

    lv_obj_add_style(lv_obj_get_child(sd_error_popup, 2), &ttf_font_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    //格式化结果显示1秒后删除提示
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_values(&anim, 0, 1);
    lv_anim_set_time(&anim, 1000);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&anim, sd_err_format_display_tips_anim_complete);
    lv_anim_start(&anim);
}

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################
// 显示SD卡异常提醒（外部调用接口）
void show_sd_error_warning(void)
{
    // 如果已有浮窗存在，return
    if (sd_error_popup) {
        return;
    }
    // 创建新浮窗
    create_sd_error_warning_popup();
}

// #endregion
// #############################################################################
// ! #region 6. 线程处理函数
// #############################################################################
// #endregion
// #############################################################################
// ! #region 7. 按键、手势、定时器 等事件回调函数
// #############################################################################

// 模态框背景点击事件回调
static void sd_error_tips_gesture_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch (code) {
    case LV_EVENT_GESTURE: {
        // 获取手势方向，需要 TP 驱动支持
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch (dir) {
        case LV_DIR_RIGHT: {
            delete_sd_error_warning();
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

// 格式化按钮事件处理
static void confir_sd_error_format_handler(lv_event_t* e)
{
    confir_format_ingb();
}

// 关闭浮窗的事件处理函数
static void cancel_close_sd_error_popup_event(lv_event_t* e)
{
    delete_sd_error_warning();
}

// #endregion
// #############################################################################
// ! #region 8. 初始化、去初始化、资源管理
// #############################################################################

// 创建SD卡异常提醒浮窗
static void create_sd_error_warning_popup(void)
{
    // 创建遮罩层
    sd_error_mask = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sd_error_mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(sd_error_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(sd_error_mask, LV_OPA_50, 0);
    lv_obj_set_style_radius(sd_error_mask, 0, 0);
    lv_obj_set_style_border_width(sd_error_mask, 0, 0);
    lv_obj_add_event_cb(sd_error_mask, sd_error_tips_gesture_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_clear_flag(sd_error_mask, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // 创建浮窗容器
    sd_error_popup = lv_obj_create(sd_error_mask);
    lv_obj_set_size(sd_error_popup, UI_X(320), UI_Y(240));
    lv_obj_center(sd_error_popup);
    lv_obj_set_style_bg_color(sd_error_popup, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(sd_error_popup, LV_OPA_100, 0);
    lv_obj_set_style_radius(sd_error_popup, 20, 0);
    lv_obj_set_style_shadow_width(sd_error_popup, 40, 0);
    lv_obj_set_style_shadow_opa(sd_error_popup, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sd_error_popup, 0, 0);
    lv_obj_clear_flag(sd_error_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(sd_error_popup, UI_X(20), 0);

    // 创建警告图标
    lv_obj_t* icon = lv_label_create(sd_error_popup);
    lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFA500), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_36, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, UI_Y(15));

    // 创建标题
    lv_obj_t* title = lv_label_create(sd_error_popup);
    lv_label_set_text(title, str_language_sd_card_abnormal[get_curr_language()]);
    lv_obj_add_style(title, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFA500), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, UI_Y(60));

    // 创建提示文本
    lv_obj_t* hint = lv_label_create(sd_error_popup);
    lv_label_set_text(hint, str_language_sd_card_read_error_suggest_format[get_curr_language()]);
    lv_obj_add_style(hint, &ttf_font_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA6ADC8), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hint, UI_X(280));
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, UI_Y(100));

    // 创建按钮容器
    lv_obj_t* btn_cont = lv_obj_create(sd_error_popup);
    lv_obj_remove_style_all(btn_cont);
    lv_obj_set_size(btn_cont, UI_X(280), UI_Y(50));
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, UI_Y(-10));
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 创建取消按钮
    lv_obj_t* btn_cancel = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_cancel, UI_X(120), UI_Y(40));
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(btn_cancel, 15, 0);

    lv_obj_t* btn_cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(btn_cancel_label, str_language_cancel[get_curr_language()]);
    lv_obj_set_style_text_color(btn_cancel_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_style(btn_cancel_label, &ttf_font_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(btn_cancel_label);

    // 创建格式化按钮
    lv_obj_t* btn_format = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_format, UI_X(120), UI_Y(40));
    lv_obj_set_style_bg_color(btn_format, lv_color_hex(0xE53935), 0);
    lv_obj_set_style_radius(btn_format, 15, 0);

    lv_obj_t* btn_format_label = lv_label_create(btn_format);
    lv_label_set_text(btn_format_label, str_language_format[get_curr_language()]);
    lv_obj_set_style_text_color(btn_format_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_style(btn_format_label, &ttf_font_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(btn_format_label);

    // 添加按钮点击事件
    lv_obj_add_event_cb(btn_cancel, cancel_close_sd_error_popup_event, LV_EVENT_CLICKED, sd_error_mask);
    lv_obj_add_event_cb(btn_format, confir_sd_error_format_handler, LV_EVENT_CLICKED, sd_error_mask);
}

// #endregion
// #############################################################################
// ! #region 9. 调试与测试
// #############################################################################
