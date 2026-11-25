#define DEBUG
#include "ui_layout.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "page_all.h"
// #include "page_sysmenu_wifilist.h"
#include "param_manager.h"
#include "ui_common.h"
#include "wifi_check_dialog.h"
#include "wifi_manager.h"

// WiFi提示窗口相关
static lv_obj_t* wifi_warning_dialog = NULL;
static lv_obj_t *wifi_warning_switch = NULL;
static lv_timer_t *wifi_check_timer = NULL;
static lv_obj_t* current_parent_screen = NULL;
static wifi_return_cb_t g_return_cb = NULL;
static void* g_return_userdata = NULL;
static wifi_release_cb_t g_release_cb = NULL;
static int g_return_flag = 0;

// 内部函数声明
static bool is_wifi_connected_internal(void);
static void wifi_warning_back_cb(lv_event_t *e);
static void wifi_warning_switch_cb(lv_event_t* e);
static void wifi_warning_enter_wifi_cb(lv_event_t* e);
static void wifi_check_timer_cb(lv_timer_t* timer);
static void show_wifi_warning_dialog_internal(lv_obj_t* parent, wifi_release_cb_t release_cb);
static int close_wifi_warning_dialog_internal(void);

void wifi_check_set_return_handler(wifi_return_cb_t cb, void* user_data)
{
    g_return_cb = cb;
    g_return_userdata = user_data;
}

wifi_return_cb_t wifi_check_get_return_handler(void)
{
    return g_return_cb;
}

void *wifi_check_get_return_userdata(void)
{
    return g_return_userdata;
}

int wifi_check_get_return_flag(void)
{
    if(g_return_flag) {
        return 1;
    } else {
        return 0;
    }
}

void wifi_check_clear_return_handler(void)
{
    //回调变量不用清除，从wifi列表返回的时候，会调用返回函数，后续还会继续注册被替换掉，不用担心
    // g_return_cb = NULL;
    // g_return_userdata = NULL;
    // g_release_cb = NULL;
    g_return_flag = 0;
}

// 检查WiFi是否已连接（内部函数）
static bool is_wifi_connected_internal(void)
{
    return param_manager_get(PARAM_ID_WIFI_CONNECTED) == 1;
}

// 公共接口：检查WiFi是否已连接
bool wifi_check_is_connected(void)
{
    return is_wifi_connected_internal();
}

// WiFi提示窗口返回按钮回调
static void wifi_warning_back_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        close_wifi_warning_dialog_internal();
    }
}

// WiFi提示窗口开关回调
static void wifi_warning_switch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *sw = lv_event_get_target(e);
        if (!check_battery_for_wifi(wifi_warning_dialog)) {
            lv_obj_clear_state(sw, LV_STATE_CHECKED); // 保持开关关闭
            return;
        }
        if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
            wifi_manager_set_enabled(1);
            // 开启WiFi后，启动定时器检查连接状态
            if (wifi_check_timer == NULL) {
                wifi_check_timer = lv_timer_create(wifi_check_timer_cb, 1000, NULL);
            }
        } else {
            wifi_manager_set_enabled(0);
            // 关闭WiFi时，删除定时器
            if (wifi_check_timer != NULL) {
                lv_timer_del(wifi_check_timer);
                wifi_check_timer = NULL;
            }
        }
    }
}

// WiFi提示窗口进入WiFi界面按钮回调
static void wifi_warning_enter_wifi_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // 关闭提示窗口
        close_wifi_warning_dialog_internal();

        // 执行资源释放回调
        if (g_release_cb != NULL) {
            g_release_cb();
        }

        g_return_flag = 1;

        // 进入WiFi列表界面
        ui_load_scr_animation(&g_ui, &sysMenu_WifiList_s, 1, NULL, sysMenu_WifiList, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
    }
}

// WiFi检查定时器回调
static void wifi_check_timer_cb(lv_timer_t *timer)
{
    if(is_wifi_connected_internal()) {
        // WiFi已连接，关闭提示窗口
        close_wifi_warning_dialog_internal();
        // 删除定时器
        if(wifi_check_timer != NULL) {
            lv_timer_del(wifi_check_timer);
            wifi_check_timer = NULL;
        }
    }
}

// 显示WiFi提示窗口（内部函数）
static void show_wifi_warning_dialog_internal(lv_obj_t* parent, wifi_release_cb_t release_cb)
{
    if (parent == NULL) {
        return;
    }

    g_release_cb = release_cb;

    if (wifi_warning_dialog != NULL && lv_obj_is_valid(wifi_warning_dialog)) {
        // 窗口已存在，直接显示
        lv_obj_clear_flag(wifi_warning_dialog, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    current_parent_screen = parent;

    // 创建提示窗口背景（半透明遮罩）
    wifi_warning_dialog = lv_obj_create(parent);
    lv_obj_set_size(wifi_warning_dialog, H_RES, V_RES);
    lv_obj_set_pos(wifi_warning_dialog, 0, 0);
    lv_obj_set_style_bg_color(wifi_warning_dialog, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(wifi_warning_dialog, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wifi_warning_dialog, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(wifi_warning_dialog, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(wifi_warning_dialog, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(wifi_warning_dialog, LV_OBJ_FLAG_SCROLLABLE);

    // 创建对话框容器
    lv_obj_t *dialog_cont = lv_obj_create(wifi_warning_dialog);
    lv_obj_set_size(dialog_cont, UI_X(500), UI_Y(300));
    lv_obj_align(dialog_cont, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog_cont, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(dialog_cont, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dialog_cont, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(dialog_cont, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(dialog_cont, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(dialog_cont, UI_X(20), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 创建提示文本
    lv_obj_t *label_title = lv_label_create(dialog_cont);
    lv_label_set_text(label_title, str_language_network_not_connected[get_curr_language()]);
    lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, UI_Y(20));

    lv_obj_t *label_hint = lv_label_create(dialog_cont);
    lv_label_set_text(label_hint, str_language_connect_to_wifi[get_curr_language()]);
    lv_obj_set_style_text_color(label_hint, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_hint, get_usr_fonts(ALI_PUHUITI_FONTPATH, 28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_hint, LV_ALIGN_TOP_MID, 0, UI_Y(70));

    // 创建返回按钮
    lv_obj_t *btn_back = lv_btn_create(dialog_cont);
    lv_obj_set_size(btn_back, UI_X(120), UI_Y(50));
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, UI_X(30), UI_Y(-30));
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, wifi_warning_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, str_language_back[get_curr_language()]);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_back, get_usr_fonts(ALI_PUHUITI_FONTPATH, 28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label_back);

    // 创建WiFi开关
    lv_obj_t *cont_switch = lv_obj_create(dialog_cont);
    lv_obj_set_size(cont_switch, UI_X(120), UI_Y(50));
    lv_obj_align(cont_switch, LV_ALIGN_BOTTOM_MID, 0, UI_Y(-30));
    lv_obj_set_style_bg_opa(cont_switch, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_switch, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont_switch, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label_switch = lv_label_create(cont_switch);
    lv_label_set_text(label_switch, "WiFi");
    lv_obj_set_style_text_color(label_switch, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_switch, get_usr_fonts(ALI_PUHUITI_FONTPATH, 24), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_switch, LV_ALIGN_LEFT_MID, 0, 0);

    wifi_warning_switch = lv_switch_create(cont_switch);
    lv_obj_align(wifi_warning_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(wifi_warning_switch, wifi_warning_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    // 检查当前WiFi状态
    if (param_manager_get(PARAM_ID_WIFI_ENABLED) == 1) {
        lv_obj_add_state(wifi_warning_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(wifi_warning_switch, LV_STATE_CHECKED);
    }

    // 创建进入WiFi界面按钮
    lv_obj_t *btn_wifi = lv_btn_create(dialog_cont);
    lv_obj_set_size(btn_wifi, UI_X(120), UI_Y(50));
    lv_obj_align(btn_wifi, LV_ALIGN_BOTTOM_RIGHT, UI_X(-30), UI_Y(-30));
    lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0x007AFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_wifi, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_wifi, wifi_warning_enter_wifi_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(label_wifi,str_language_wifi_list[get_curr_language()]);
    lv_obj_set_style_text_color(label_wifi, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_wifi, get_usr_fonts(ALI_PUHUITI_FONTPATH, 28), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label_wifi);
}

// 关闭WiFi提示窗口（内部函数）
static int close_wifi_warning_dialog_internal(void)
{
    int ret = 0;
    if(wifi_warning_dialog != NULL) {
        if(lv_obj_is_valid(wifi_warning_dialog)) {
            lv_obj_del(wifi_warning_dialog);
        }
        wifi_warning_dialog = NULL;
        wifi_warning_switch = NULL;
        current_parent_screen = NULL;
        ret = 1;
    }
    if(wifi_check_timer != NULL) {
        lv_timer_del(wifi_check_timer);
        wifi_check_timer = NULL;
        ret = 1;
    }
    delete_batter_tips_mbox();
    return ret;
}

// 公共接口：检查WiFi连接状态，如果未连接则显示提示对话框
bool wifi_check_and_show_dialog(lv_obj_t* parent_screen,
    wifi_return_cb_t return_cb, wifi_release_cb_t release_cb,
    void* user_data)
{
    if (is_wifi_connected_internal()) {
        return true;  // WiFi已连接，无需显示对话框
    }

    wifi_check_set_return_handler(return_cb, user_data);

    // WiFi未连接，显示对话框
    show_wifi_warning_dialog_internal(parent_screen, release_cb);
    return false; // WiFi未连接，已显示对话框
}

// 公共接口：关闭WiFi检查对话框
int wifi_check_dialog_close(void)
{
    wifi_check_clear_return_handler();
    return close_wifi_warning_dialog_internal();
}
