// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#define DEBUG
#include "page_wifi_list.h"
#include "batter_low_tips.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "mlog.h"
#include "multilang_strings.h"
#include "page_all.h"
#include "style_common.h"
#include "top_notice.h"
#include "wifi_manager.h"
// #include "ui/gesture_back.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RES_ICON_PATH "/mnt/data/bin/res"
// #endregion
// #############################################################################
// ! #region 2. 数据结构定义 (见 page_wifi_list.h)
// #############################################################################

// #endregion
// #############################################################################
// ! #region 3. 全局变量 & 函数声明
// #############################################################################

lv_obj_t* sysMenu_WifiList_s; // wifi 页面对象
void* private_data = NULL; // wifi 页面私有数据指针

static void rebuild_wifi_list(page_wifi_list_data_t* data);
static void wifi_item_click_cb(lv_event_t* e);
static void refresh_btn_click_cb(lv_event_t* e);
static void wifi_switch_change_cb(lv_event_t* e);
static void password_cancel_btn_cb(lv_event_t* e);
static void password_confirm_btn_cb(lv_event_t* e);
static void password_keyboard_event_cb(lv_event_t* e);
static void show_password_modal(page_wifi_list_data_t* data, const char* ssid);
static void hide_password_modal(page_wifi_list_data_t* data);
static void connect_wifi_and_refresh(page_wifi_list_data_t* data, const char* ssid, const char* password);
static void start_wifi_scan(page_wifi_list_data_t* data);
static void start_wifi_scan_with_notice(page_wifi_list_data_t* data, uint8_t show_notice);
static void scan_timer_cb(lv_timer_t* timer);
static void connect_timer_cb(lv_timer_t* timer);
static void switch_apply_timer_cb(lv_timer_t* timer);
static void append_wifi_list_item(page_wifi_list_data_t* data, int index);
static void stop_connect_timer(page_wifi_list_data_t* data);
static void sync_wifi_enabled_state(page_wifi_list_data_t* data);
static void set_wifi_switch_checked(page_wifi_list_data_t* data, uint8_t checked);
static void set_wifi_switch_busy(page_wifi_list_data_t* data, uint8_t busy);

// WiFi列表焦点选择相关函数
static void wifi_list_select_next(page_wifi_list_data_t* data);
static void wifi_list_select_prev(page_wifi_list_data_t* data);
static void wifi_list_confirm_selection(page_wifi_list_data_t* data);
static void wifi_list_clear_selection(page_wifi_list_data_t* data);
static void wifi_list_set_selection(page_wifi_list_data_t* data, int index);
static int wifi_list_get_selected_index(page_wifi_list_data_t* data);
static void wifi_list_key_handler(int key_code, int key_value);

#define WIFI_CONNECT_POLL_MS 500
#define WIFI_CONNECT_MAX_POLLS 40
#define WIFI_CONNECT_TIMEOUT_MS (WIFI_CONNECT_POLL_MS * WIFI_CONNECT_MAX_POLLS)
#define WIFI_SWITCH_APPLY_DELAY_MS 20

// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################

static void page_set_private_data(void* data)
{
    private_data = data;
}

static void* page_get_private_data(void)
{
    return private_data;
}

/* 仅清理页面运行态异步资源，不释放页面对象本身。 */
static void cleanup_wifi_page_runtime(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        return;
    }

    if (data->scan_timer != NULL) {
        lv_timer_del(data->scan_timer);
        data->scan_timer = NULL;
    }
    if (data->switch_apply_timer != NULL) {
        lv_timer_del(data->switch_apply_timer);
        data->switch_apply_timer = NULL;
    }
    data->switch_apply_pending = 0;
    set_wifi_switch_busy(data, 0);
    stop_connect_timer(data);
}

/* 统一清理 WiFi 页面私有资源，避免页面重建时残留定时器/悬空指针。 */
static void cleanup_wifi_page_data(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        page_set_private_data(NULL);
        sysMenu_WifiList_s = NULL;
        return;
    }

    cleanup_wifi_page_runtime(data);

    if (data->container != NULL) {
        if (lv_obj_is_valid(data->container)) {
            lv_obj_del(data->container);
        }
        data->container = NULL;
    }

    free(data);
    page_set_private_data(NULL);
    sysMenu_WifiList_s = NULL;
}

/* 根据是否加密选择 WiFi 列表图标。 */
static const char* get_wifi_icon_path(const wifi_ap_info_t* ap)
{
    if (ap != NULL && ap->is_secured) {
        return "A:" RES_ICON_PATH "/sys-wifi-lock.png";
    }
    return "A:" RES_ICON_PATH "/sys-wifi.png";
}

static void append_wifi_list_item(page_wifi_list_data_t* data, int index)
{
    lv_obj_t* btn;
    lv_obj_t* label;
    const char* icon_path;
    wifi_ap_info_t* ap;

    if (data == NULL || data->wifi_list == NULL || index < 0 || index >= data->scan_count) {
        MLOG_ERR("[WIFI_ITEM] invalid params!\n");
        return;
    }

    if (!lv_obj_is_valid(data->wifi_list)) {
        MLOG_ERR("[WIFI_ITEM] wifi_list is not valid!\n");
        return;
    }

    ap = &data->scan_results[index];
    icon_path = get_wifi_icon_path(ap);
    btn = lv_list_add_btn(data->wifi_list, icon_path, ap->ssid);
    if (btn == NULL) {
        MLOG_ERR("[WIFI_ITEM] btn is NULL!\n");
        return;
    }

    lv_obj_add_style(btn, &style_settings_item, LV_PART_MAIN);
    lv_obj_add_style(btn, &NORMAL_SIZE, LV_PART_MAIN);
    lv_obj_add_style(btn, (index % 2 == 0) ? &style_list_row_even : &style_list_row_odd, LV_PART_MAIN);

    label = lv_obj_get_child(btn, 1);
    if (label != NULL) {
        lv_obj_add_style(label, &NORMAL_SIZE, LV_PART_MAIN);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_width(label, lv_pct(78));
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 44, 0);
    }

    if (ap->is_connected) {
        lv_obj_t* connected_icon = lv_img_create(btn);
        lv_img_set_src(connected_icon, "A:" RES_ICON_PATH "/check.png");
        lv_obj_align(connected_icon, LV_ALIGN_RIGHT_MID, -10, 0);
    } else if (ap->is_saved) {
        lv_obj_t* saved_icon = lv_img_create(btn);
        lv_img_set_src(saved_icon, "A:" RES_ICON_PATH "/wifi-saved-unlock.png");
        lv_obj_align(saved_icon, LV_ALIGN_RIGHT_MID, -10, 0);
    }

    lv_obj_set_user_data(btn, ap);
    lv_obj_add_event_cb(btn, wifi_item_click_cb, LV_EVENT_CLICKED, data);
}

/* 显示密码输入弹层并绑定目标 SSID。 */
static void show_password_modal(page_wifi_list_data_t* data, const char* ssid)
{
    char title[128];

    if (data == NULL || data->password_modal_mask == NULL || data->password_ta == NULL) {
        return;
    }

    if (ssid == NULL) {
        ssid = "";
    }

    lv_snprintf(data->pending_ssid, sizeof(data->pending_ssid), "%s", ssid);
    lv_snprintf(title, sizeof(title), str_language_enter_password_for[get_curr_language()], data->pending_ssid);
    lv_label_set_text(data->password_ssid_label, title);
    lv_textarea_set_text(data->password_ta, "");
    lv_keyboard_set_textarea(data->password_kb, data->password_ta);
    lv_obj_clear_flag(data->password_modal_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(data->password_modal_mask);
    lv_obj_add_state(data->password_ta, LV_STATE_FOCUSED);
}

/* 隐藏密码输入弹层。 */
static void hide_password_modal(page_wifi_list_data_t* data)
{
    if (data == NULL || data->password_modal_mask == NULL) {
        return;
    }

    lv_obj_add_flag(data->password_modal_mask, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(data->password_ta, LV_STATE_FOCUSED);
}

static void stop_connect_timer(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        return;
    }
    if (data->connect_timer != NULL) {
        lv_timer_del(data->connect_timer);
        data->connect_timer = NULL;
    }
    data->connect_poll_count = 0;
    data->connecting_ssid[0] = '\0';
}

// 清除之前选中的样式
static void wifi_list_clear_selection(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        return;
    }
    if (data->current_selected_btn != NULL) {
        lv_obj_set_style_border_color(data->current_selected_btn, lv_color_hex(0x3A3A3A), LV_PART_MAIN);
        lv_obj_set_style_border_width(data->current_selected_btn, 0, LV_PART_MAIN);
        data->current_selected_btn = NULL;
    }
}

// 设置选中样式（蓝色边框）
static void wifi_list_set_selection(page_wifi_list_data_t* data, int index)
{
    if (data == NULL || data->wifi_list == NULL) {
        return;
    }
    if (index < 0 || index >= data->scan_count) {
        return;
    }

    wifi_list_clear_selection(data);

    lv_obj_t* btn = lv_obj_get_child(data->wifi_list, index);
    if (btn != NULL) {
        lv_obj_set_style_border_color(btn, lv_color_hex(0x007AFF), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
        data->current_selected_btn = btn;
        data->current_wifi_index = index;
        // 滚动到选中项
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);
    }
}

static int wifi_list_get_selected_index(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        return -1;
    }
    return data->current_wifi_index;
}

// 选择下一个WiFi项
static void wifi_list_select_next(page_wifi_list_data_t* data)
{
    if (data == NULL || data->scan_count == 0) {
        return;
    }

    int new_index;
    if (data->current_wifi_index == -1) {
        new_index = 0;
    } else {
        new_index = (data->current_wifi_index + 1) % data->scan_count;
    }
    wifi_list_set_selection(data, new_index);
}

// 选择上一个WiFi项
static void wifi_list_select_prev(page_wifi_list_data_t* data)
{
    if (data == NULL || data->scan_count == 0) {
        return;
    }

    int new_index;
    if (data->current_wifi_index == -1) {
        new_index = data->scan_count - 1;
    } else {
        new_index = (data->current_wifi_index - 1 + data->scan_count) % data->scan_count;
    }
    wifi_list_set_selection(data, new_index);
}

// 确认选择当前WiFi
static void wifi_list_confirm_selection(page_wifi_list_data_t* data)
{
    if (data == NULL) {
        return;
    }
    if (data->current_wifi_index >= 0 && data->current_wifi_index < data->scan_count) {
        wifi_ap_info_t* ap = &data->scan_results[data->current_wifi_index];
        if (ap->is_connected) {
            top_notice_show(str_language_wifi_connected[get_curr_language()], TOP_NOTICE_TYPE_SUCCESS);
            return;
        }
        if (ap->is_secured && !ap->is_saved) {
            show_password_modal(data, ap->ssid);
            return;
        }
        connect_wifi_and_refresh(data, ap->ssid, "");
    }
}

static void sync_wifi_enabled_state(page_wifi_list_data_t* data)
{
    int enabled_now;

    if (data == NULL || data->wifi_switch == NULL) {
        return;
    }

    enabled_now = wifi_manager_get_status() == 1 ? 1 : 0;
    data->wifi_enabled = (uint8_t)enabled_now;
    set_wifi_switch_checked(data, (uint8_t)enabled_now);
}

static void set_wifi_switch_checked(page_wifi_list_data_t* data, uint8_t checked)
{
    if (data == NULL || data->wifi_switch == NULL || !lv_obj_is_valid(data->wifi_switch)) {
        return;
    }

    data->switch_ignore_event = 1;
    if (checked) {
        lv_obj_add_state(data->wifi_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(data->wifi_switch, LV_STATE_CHECKED);
    }
    data->switch_ignore_event = 0;
}

static void set_wifi_switch_busy(page_wifi_list_data_t* data, uint8_t busy)
{
    if (data == NULL || data->wifi_switch == NULL || !lv_obj_is_valid(data->wifi_switch)) {
        return;
    }

    if (busy) {
        lv_obj_add_state(data->wifi_switch, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(data->wifi_switch, LV_STATE_DISABLED);
    }
}

/* 封装连接流程，统一提示与列表刷新。 */
static void connect_wifi_and_refresh(page_wifi_list_data_t* data, const char* ssid, const char* password)
{
    int ret;

    if (data == NULL || ssid == NULL || ssid[0] == '\0') {
        return;
    }
    if (data->wifi_enabled == 0) {
        top_notice_show_for(str_language_wifi_turned_off[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 2000);
        return;
    }
    if (data->connect_timer != NULL) {
        top_notice_show_for(str_language_wifi_connect_task_running[get_curr_language()], TOP_NOTICE_TYPE_WARNING, WIFI_CONNECT_TIMEOUT_MS);
        return;
    }

    ret = wifi_manager_connect_async(ssid, password);
    if (ret == WIFI_CONNECT_OK) {
        lv_snprintf(data->connecting_ssid, sizeof(data->connecting_ssid), "%s", ssid);
        data->connect_poll_count = 0;
        data->connect_timer = lv_timer_create(connect_timer_cb, WIFI_CONNECT_POLL_MS, data);
        top_notice_show_for(str_language_wifi_connecting[get_curr_language()], TOP_NOTICE_TYPE_WARNING, WIFI_CONNECT_TIMEOUT_MS);
        return;
    }

    if (ret == WIFI_CONNECT_ERR_DISABLED) {
        top_notice_show(str_language_wifi_connect_failed_disabled[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
    } else if (ret == WIFI_CONNECT_ERR_BUSY) {
        top_notice_show(str_language_wifi_connect_failed_busy[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
    } else {
        top_notice_show(str_language_wifi_connect_failed_network[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
    }
}

static void connect_timer_cb(lv_timer_t* timer)
{
    page_wifi_list_data_t* data;
    int state = 0;
    int error = WIFI_CONNECT_OK;
    int i;
    char ssid[WIFI_MANAGER_MAX_SSID_LEN];

    if (timer == NULL) {
        MLOG_ERR("[WIFI_CONN] ERROR: timer is NULL!\n");
        return;
    }

    data = (page_wifi_list_data_t*)lv_timer_get_user_data(timer);

    /* 重要：检查 private_data 是否为 NULL，防止竞态条件导致访问已释放内存 */
    if (page_get_private_data() == NULL) {
        MLOG_WARN("[WIFI_CONN] page is being destroyed, ignoring callback\n");
        stop_connect_timer(data);
        return;
    }

    if (data == NULL || data->container == NULL || !lv_obj_is_valid(data->container)) {
        MLOG_ERR("[WIFI_CONN] ERROR: invalid container!\n");
        stop_connect_timer(data);
        return;
    }

    if (data->wifi_enabled == 0) {
        stop_connect_timer(data);
        return;
    }

    data->connect_poll_count++;
    if (data->connect_poll_count >= WIFI_CONNECT_MAX_POLLS) {
        stop_connect_timer(data);
        top_notice_show(str_language_wifi_connect_failed_result_timeout[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
        return;
    }

    if (wifi_manager_get_connect_result(&state, &error, ssid, sizeof(ssid)) != 0) {
        return;
    }

    if (state == WIFI_CONNECT_STATE_CONNECTING) {
        return;
    }

    if (state == WIFI_CONNECT_STATE_CONNECTED) {
        for (i = 0; i < data->scan_count; i++) {
            data->scan_results[i].is_connected = 0;
            if (strcmp(data->scan_results[i].ssid, ssid) == 0) {
                data->scan_results[i].is_connected = 1;
                data->scan_results[i].is_saved = 1;
            }
        }
        rebuild_wifi_list(data);
        stop_connect_timer(data);
        top_notice_show(str_language_wifi_connect_success[get_curr_language()], TOP_NOTICE_TYPE_SUCCESS);
        start_wifi_scan_with_notice(data, 0);
        return;
    }

    if (state == WIFI_CONNECT_STATE_FAILED) {
        if (error == WIFI_CONNECT_ERR_AUTH) {
            for (i = 0; i < data->scan_count; i++) {
                if (strcmp(data->scan_results[i].ssid, ssid) == 0) {
                    data->scan_results[i].is_saved = 0;
                    data->scan_results[i].is_connected = 0;
                    break;
                }
            }
            top_notice_show(str_language_wifi_connect_failed_auth[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
        } else if (error == WIFI_CONNECT_ERR_TIMEOUT) {
            top_notice_show(str_language_wifi_connect_failed_timeout[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
        } else {
            top_notice_show(str_language_wifi_connect_failed_network[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
        }
        rebuild_wifi_list(data);
        stop_connect_timer(data);
        return;
    }

    if (state == WIFI_CONNECT_STATE_IDLE) {
        stop_connect_timer(data);
    }
}

/* 扫描并重建 WiFi 列表。 */
static void rebuild_wifi_list(page_wifi_list_data_t* data)
{
    int i;

    if (data == NULL || data->wifi_list == NULL || !lv_obj_is_valid(data->wifi_list)) {
        MLOG_ERR("[WIFI_LIST] ERROR: invalid wifi_list!\n");
        return;
    }

    if (data->wifi_enabled == 0) {
        /* 重要：清除悬空指针 */
        data->current_selected_btn = NULL;
        data->current_wifi_index = -1;
        lv_obj_clean(data->wifi_list);
        return;
    }

    /* 重要：清除悬空指针，避免访问已删除的LVGL对象导致崩溃 */
    data->current_selected_btn = NULL;
    data->current_wifi_index = -1;
    lv_obj_clean(data->wifi_list);

    for (i = 0; i < data->scan_count; i++) {
        append_wifi_list_item(data, i);
    }
}

/* 开始 WiFi 扫描 */
static void start_wifi_scan(page_wifi_list_data_t* data)
{
    start_wifi_scan_with_notice(data, 1);
}

static void start_wifi_scan_with_notice(page_wifi_list_data_t* data, uint8_t show_notice)
{
    if (data == NULL) {
        MLOG_ERR("[WIFI_SCAN] start_wifi_scan_with_notice: data is NULL!\n");
        return;
    }

    /* 停止之前的定时器 */
    if (data->scan_timer != NULL) {
        lv_timer_del(data->scan_timer);
        data->scan_timer = NULL;
    }

    /* 启动扫描 */
    data->pending_scan_id = wifi_manager_start_scan();
    if (data->pending_scan_id < 0) {
        // if (show_notice) {
        //     top_notice_show_for(str_language_wifi_scan_failed[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 2000);
        // }
        lv_obj_send_event(data->refresh_btn, LV_EVENT_CLICKED, NULL);
        return;
    }

    if (show_notice) {
        top_notice_show_for(str_language_wifi_scan_started[get_curr_language()], TOP_NOTICE_TYPE_INFO, 5000);
    }

    /* 创建 1 秒定时器轮询结果 */
    data->scan_timer = lv_timer_create(scan_timer_cb, 1000, data);
}

static void switch_apply_timer_cb(lv_timer_t* timer)
{
    page_wifi_list_data_t* data;
    int enabled;

    if (timer == NULL) {
        return;
    }

    data = (page_wifi_list_data_t*)lv_timer_get_user_data(timer);

    /* 重要：检查 private_data 是否为 NULL，防止竞态条件导致访问已释放内存 */
    if (page_get_private_data() == NULL) {
        MLOG_WARN("[WIFI_SWITCH] page is being destroyed, ignoring callback\n");
        return;
    }

    if (data == NULL || data->container == NULL) {
        return;
    }

    data->switch_apply_timer = NULL;
    enabled = data->switch_target_enabled ? 1 : 0;

    if (wifi_manager_set_enabled(enabled) != 0) {
        top_notice_show_for(enabled ? str_language_wifi_turn_on_failed[get_curr_language()] : str_language_wifi_turn_off_failed[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 2000);
        set_wifi_switch_checked(data, data->wifi_enabled);
        data->switch_apply_pending = 0;
        set_wifi_switch_busy(data, 0);
        return;
    }

    data->wifi_enabled = (uint8_t)enabled;
    if (enabled) {
        start_wifi_scan(data);
        top_notice_show_for(str_language_wifi_turned_on[get_curr_language()], TOP_NOTICE_TYPE_SUCCESS, 2000);
    } else {
        stop_connect_timer(data);
        /* 停止扫描定时器 */
        if (data->scan_timer != NULL) {
            lv_timer_del(data->scan_timer);
            data->scan_timer = NULL;
        }
        /* 重要：清除悬空指针 */
        data->current_selected_btn = NULL;
        data->current_wifi_index = -1;
        lv_obj_clean(data->wifi_list);
        top_notice_show_for(str_language_wifi_turned_off[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 2000);
    }

    data->switch_apply_pending = 0;
    set_wifi_switch_busy(data, 0);
}

/* 扫描定时器回调 */
static void scan_timer_cb(lv_timer_t* timer)
{
    page_wifi_list_data_t* data;
    int count;
    int i;

    if (timer == NULL) {
        MLOG_ERR("[WIFI_SCAN] ERROR: timer is NULL!\n");
        return;
    }

    data = (page_wifi_list_data_t*)lv_timer_get_user_data(timer);
    if (data == NULL) {
        MLOG_ERR("[WIFI_SCAN] ERROR: data is NULL!\n");
        lv_timer_del(timer);
        return;
    }

    /* 重要：检查 private_data 是否为 NULL，防止竞态条件导致访问已释放内存 */
    if (page_get_private_data() == NULL) {
        MLOG_WARN("[WIFI_SCAN] page is being destroyed, ignoring callback\n");
        data->scan_timer = NULL;
        lv_timer_del(timer);
        return;
    }

    if (data->wifi_list == NULL) {
        MLOG_ERR("[WIFI_SCAN] ERROR: wifi_list is NULL!\n");
        data->scan_timer = NULL;
        lv_timer_del(timer);
        return;
    }

    if (!lv_obj_is_valid(data->wifi_list)) {
        MLOG_ERR("[WIFI_SCAN] ERROR: wifi_list is not valid!\n");
        data->scan_timer = NULL;
        lv_timer_del(timer);
        return;
    }

    if (data->wifi_enabled == 0) {
        lv_timer_del(timer);
        data->scan_timer = NULL;
        return;
    }

    /* 获取扫描结果 */
    count = wifi_manager_get_scan_results(data->scan_results, WIFI_LIST_MAX_AP_COUNT, data->pending_scan_id);
    if (count < 0) {
        /* 还在扫描中，继续等待 */
        return;
    }

    /* 扫描完成 */
    data->scan_count = count;
    lv_timer_del(timer);
    data->scan_timer = NULL;

    /* 重建列表 */
    /* 重要：清除悬空指针，避免访问已删除的LVGL对象导致崩溃 */
    data->current_selected_btn = NULL;
    lv_obj_clean(data->wifi_list);

    for (i = 0; i < count; i++) {
        append_wifi_list_item(data, i);
    }

    /* 扫描完成后默认选中第一个WiFi项 */
    if (count > 0) {
        data->current_wifi_index = -1;
        wifi_list_select_next(data);
    } else {
        data->current_wifi_index = -1;
        data->current_selected_btn = NULL;
    }

    char notice_text[64];
    snprintf(notice_text, sizeof(notice_text), str_language_wifi_found_count[get_curr_language()], count);
    top_notice_show(notice_text, TOP_NOTICE_TYPE_INFO);
}

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################

// #endregion
// #############################################################################
// ! #region 6. 线程处理函数
// #############################################################################

// #endregion
// #############################################################################
// ! #region 7. 按键、手势、定时器 等事件回调函数
// #############################################################################

static void wifi_item_click_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    lv_obj_t* target = lv_event_get_target(e);
    wifi_ap_info_t* ap = NULL;

    if (data == NULL || target == NULL) {
        return;
    }

    ap = (wifi_ap_info_t*)lv_obj_get_user_data(target);
    if (ap == NULL) {
        return;
    }
    if (ap->is_connected) {
        top_notice_show(str_language_wifi_connected[get_curr_language()], TOP_NOTICE_TYPE_SUCCESS);
        return;
    }

    if (ap->is_secured && !ap->is_saved) {
        show_password_modal(data, ap->ssid);
        return;
    }
    connect_wifi_and_refresh(data, ap->ssid, "");
}

static void password_cancel_btn_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    if (data == NULL) {
        return;
    }

    hide_password_modal(data);
}

static void password_confirm_btn_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    const char* password;

    if (data == NULL || data->password_ta == NULL) {
        return;
    }

    password = lv_textarea_get_text(data->password_ta);
    if (password == NULL || password[0] == '\0') {
        top_notice_show(str_language_wifi_password_required[get_curr_language()], TOP_NOTICE_TYPE_ERROR);
        return;
    }

    connect_wifi_and_refresh(data, data->pending_ssid, password);
    hide_password_modal(data);
}

static void password_keyboard_event_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (data == NULL) {
        return;
    }

    if (code == LV_EVENT_READY) {
        password_confirm_btn_cb(e);
    } else if (code == LV_EVENT_CANCEL) {
        hide_password_modal(data);
    }
}

static void refresh_btn_click_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    if (data == NULL) {
        MLOG_ERR("[WIFI_SCAN] refresh_btn_click_cb: data is NULL!\n");
        return;
    }

    if (data->wifi_enabled == 0) {
        top_notice_show_for(str_language_wifi_turned_off[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 2000);
        return;
    }

    start_wifi_scan(data);
}

static void wifi_switch_change_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)lv_event_get_user_data(e);
    int enabled;

    if (data == NULL || data->wifi_switch == NULL) {
        return;
    }

    if (data->switch_ignore_event) {
        return;
    }

    if (data->switch_apply_pending) {
        set_wifi_switch_checked(data, data->wifi_enabled);
        top_notice_show_for(str_language_wifi_switching[get_curr_language()], TOP_NOTICE_TYPE_WARNING, 1500);
        return;
    }

    enabled = lv_obj_has_state(data->wifi_switch, LV_STATE_CHECKED) ? 1 : 0;

    /* 尝试打开WiFi时检查电量 */
    if (enabled == 1 && !check_battery_for_wifi(data->container)) {
        set_wifi_switch_checked(data, 0); // 保持开关关闭
        return;
    }

    data->switch_target_enabled = (uint8_t)enabled;
    data->switch_apply_pending = 1;
    set_wifi_switch_busy(data, 1);
    top_notice_show_for(enabled ? str_language_wifi_turning_on[get_curr_language()] : str_language_wifi_turning_off[get_curr_language()], TOP_NOTICE_TYPE_INFO, 1200);

    if (data->switch_apply_timer != NULL) {
        lv_timer_del(data->switch_apply_timer);
        data->switch_apply_timer = NULL;
    }
    data->switch_apply_timer = lv_timer_create(switch_apply_timer_cb, WIFI_SWITCH_APPLY_DELAY_MS, data);
    if (data->switch_apply_timer != NULL) {
        lv_timer_set_repeat_count(data->switch_apply_timer, 1);
    }
}

// 返回按钮回调函数
static void wifi_back_cb(lv_event_t* e)
{
    page_wifi_list_data_t* data;

    UNUSED(e);
    data = (page_wifi_list_data_t*)page_get_private_data();
    cleanup_wifi_page_runtime(data);

    /* 取消按键处理注册 */
    set_current_page_handler(NULL);

    if (wifi_check_get_return_flag() == 1) {
        wifi_return_cb_t cb = wifi_check_get_return_handler();
        wifi_return_cb_t handler = cb;
        void* userdata = wifi_check_get_return_userdata();
        wifi_check_clear_return_handler();
        handler(userdata);
    } else {
        // 返回系统设置页面
        delete_batter_tips_mbox();
        extern bool is_factory_entry_wifi;
        if (is_factory_entry_wifi == true) {
            MLOG_DBG("act_scr=%p, obj_factory_test_s=%p, valid=%d\n",
                lv_screen_active(), obj_factory_test_s, lv_obj_is_valid(obj_factory_test_s));
            ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
                LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
        } else {
            ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                true);
        }
    }
}

static void gesture_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE: {
        // 获取手势方向，需要 TP 驱动支持
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch (dir) {
        case LV_DIR_RIGHT: {
            MLOG_DBG("向右滑动，返回页面\n");
            page_wifi_list_data_t* data;
            UNUSED(e);
            data = (page_wifi_list_data_t*)page_get_private_data();
            cleanup_wifi_page_runtime(data);

            if (wifi_check_get_return_flag() == 1) {
                wifi_return_cb_t cb = wifi_check_get_return_handler();
                wifi_return_cb_t handler = cb;
                void* userdata = wifi_check_get_return_userdata();
                wifi_check_clear_return_handler();
                handler(userdata);
            } else {
                // 返回系统设置页面
                delete_batter_tips_mbox();
                ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting,
                    LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
            }
            break;
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

// WiFi列表按键处理函数
static void wifi_list_key_handler(int key_code, int key_value)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)page_get_private_data();

    if (key_value == 1) { // 按键按下
        switch (key_code) {
        case KEY_MENU: {
            MLOG_DBG("返回页面\n");
            cleanup_wifi_page_runtime(data);

            if (wifi_check_get_return_flag() == 1) {
                wifi_return_cb_t cb = wifi_check_get_return_handler();
                wifi_return_cb_t handler = cb;
                void* userdata = wifi_check_get_return_userdata();
                wifi_check_clear_return_handler();
                handler(userdata);
            } else {
                // 返回系统设置页面
                delete_batter_tips_mbox();
                extern bool is_factory_entry_wifi;
                if (is_factory_entry_wifi == true) {
                    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
                        LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
                } else {
                    ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting,
                        LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                }
            }
            break;
        }

        case KEY_UP: {
            if (data != NULL) {
                wifi_list_select_prev(data);
            }
            break;
        }

        case KEY_DOWN: {
            if (data != NULL) {
                wifi_list_select_next(data);
            }
            break;
        }

        case KEY_OK: {
            if (data != NULL) {
                wifi_list_confirm_selection(data);
            }
            break;
        }

        default:
            break;
        }
    }
}

// #endregion
// #############################################################################
// ! #region 8. 初始化、去初始化、资源管理
// #############################################################################

// void page_wifi_list_create(void)
void sysMenu_WifiList(lv_ui_t* ui)
{
    page_wifi_list_data_t* data;
    page_wifi_list_data_t* old_data;
    lv_obj_t* back_btn;
    lv_obj_t* back_icon;
    lv_obj_t* title_label;
    lv_obj_t* refresh_icon;
    lv_obj_t* cancel_label;
    lv_obj_t* confirm_label;

    /* 重建页面前先清理旧 private_data 与异步资源。 */
    old_data = (page_wifi_list_data_t*)page_get_private_data();
    if (old_data != NULL) {
        cleanup_wifi_page_data(old_data);
    } else if (sysMenu_WifiList_s != NULL) {
        if (lv_obj_is_valid(sysMenu_WifiList_s)) {
            lv_obj_del(sysMenu_WifiList_s);
        }
        sysMenu_WifiList_s = NULL;
        page_set_private_data(NULL);
    }

    data = (page_wifi_list_data_t*)malloc(sizeof(page_wifi_list_data_t));
    if (data == NULL) {
        return;
    }
    memset(data, 0, sizeof(*data));

    /* 页面根容器。 */
    data->container = lv_obj_create(NULL);
    sysMenu_WifiList_s = data->container;
    lv_obj_set_width(data->container, H_RES);
    lv_obj_set_height(data->container, V_RES);
    lv_obj_add_style(data->container, &style_page_container, LV_PART_MAIN);
    lv_obj_set_layout(data->container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(data->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(data->container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(data->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(data->container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(data->container, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(data->container, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(data->container, gesture_event_handler, LV_EVENT_GESTURE, ui);

    /* 顶部导航栏容器。 */
    data->nav_bar = lv_obj_create(data->container);
    lv_obj_set_width(data->nav_bar, lv_pct(100));
    lv_obj_set_height(data->nav_bar, 60);
    lv_obj_set_scrollbar_mode(data->nav_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(data->nav_bar, &style_common_cont_top, LV_PART_MAIN);

    back_btn = lv_button_create(data->nav_bar);
    lv_obj_set_pos(back_btn, 4, 4);
    lv_obj_set_size(back_btn, 100, 52);
    lv_obj_add_event_cb(back_btn, wifi_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_style(back_btn, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(back_icon, LV_LABEL_LONG_WRAP);
    lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(back_icon, LV_PCT(100));
    lv_obj_add_style(back_icon, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 导航栏标题。 */
    title_label = lv_label_create(data->nav_bar);
    lv_label_set_text(title_label, str_language_wifi_list[get_curr_language()]);
    lv_obj_set_size(title_label, 160, 52);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL);
    lv_obj_add_style(title_label, &NORMAL_SIZE, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 10);

    /* 右上角刷新按钮。 */
    data->refresh_btn = lv_btn_create(data->nav_bar);
    lv_obj_set_size(data->refresh_btn, 100, 52);
    lv_obj_add_style(data->refresh_btn, &style_common_btn_back, LV_PART_MAIN);
    // lv_obj_set_style_radius(data->refresh_btn, 8, LV_PART_MAIN);
    lv_obj_align(data->refresh_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(data->refresh_btn, refresh_btn_click_cb, LV_EVENT_CLICKED, data);

    /* 刷新按钮图标。 */
    refresh_icon = lv_img_create(data->refresh_btn);
    lv_img_set_src(refresh_icon, "A:" RES_ICON_PATH "/refresh.png");
    lv_obj_align(refresh_icon, LV_ALIGN_CENTER, 0, 0);

    /* 右上角 WiFi 开关。 */
    data->wifi_switch = lv_switch_create(data->nav_bar);
    lv_obj_set_size(data->wifi_switch, 80, 52);
    lv_obj_align_to(data->wifi_switch, title_label, LV_ALIGN_OUT_RIGHT_MID, 10, -10);
    /* 获取 WiFi 状态并设置开关 */
    data->wifi_enabled = wifi_manager_get_status() == 1 ? 1 : 0;
    if (data->wifi_enabled) {
        lv_obj_add_state(data->wifi_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(data->wifi_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(data->wifi_switch, wifi_switch_change_cb, LV_EVENT_VALUE_CHANGED, data);

    /* 密码输入流程的全屏遮罩。 */
    data->password_modal_mask = lv_obj_create(data->container);
    lv_obj_add_flag(data->password_modal_mask, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(data->password_modal_mask, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(data->password_modal_mask, &style_overlay_mask, LV_PART_MAIN);
    lv_obj_add_flag(data->password_modal_mask, LV_OBJ_FLAG_HIDDEN);

    /* 密码输入面板。 */
    data->password_panel = lv_obj_create(data->password_modal_mask);
    lv_obj_set_width(data->password_panel, LV_PCT(92));
    lv_obj_set_height(data->password_panel, 170);
    lv_obj_add_style(data->password_panel, &style_modal_panel, LV_PART_MAIN);
    lv_obj_clear_flag(data->password_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(data->password_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(data->password_panel, LV_ALIGN_TOP_MID, 0, 50);

    /* 面板标题（显示当前输入的 SSID）。 */
    data->password_ssid_label = lv_label_create(data->password_panel);
    lv_label_set_text(data->password_ssid_label, str_language_input_password_text[get_curr_language()]);
    lv_obj_add_style(data->password_ssid_label, &SMALL_SIZE, LV_PART_MAIN);
    lv_obj_align(data->password_ssid_label, LV_ALIGN_TOP_LEFT, 12, 10);

    /* 密码输入框。 */
    data->password_ta = lv_textarea_create(data->password_panel);
    lv_obj_set_width(data->password_ta, LV_PCT(94));
    lv_obj_set_height(data->password_ta, 46);
    lv_obj_align(data->password_ta, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_clear_flag(data->password_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(data->password_ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_one_line(data->password_ta, true);
    lv_textarea_set_password_mode(data->password_ta, false);
    lv_textarea_set_placeholder_text(data->password_ta, str_language_input_wifi_password[get_curr_language()]);
    lv_obj_add_style(data->password_ta, &SMALL_SIZE, LV_PART_MAIN);
    lv_obj_set_style_border_width(data->password_ta, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(data->password_ta, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(data->password_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(data->password_ta, lv_color_hex(0x808080), LV_PART_CURSOR | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(data->password_ta, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(data->password_ta, lv_color_hex(0xA0A0A0), LV_PART_TEXTAREA_PLACEHOLDER);

    /* 取消按钮。 */
    data->password_cancel_btn = lv_btn_create(data->password_panel);
    lv_obj_set_size(data->password_cancel_btn, 112, 38);
    lv_obj_align(data->password_cancel_btn, LV_ALIGN_BOTTOM_LEFT, 12, 2);
    lv_obj_add_event_cb(data->password_cancel_btn, password_cancel_btn_cb, LV_EVENT_CLICKED, data);
    /* 取消按钮文字。 */
    cancel_label = lv_label_create(data->password_cancel_btn);
    lv_label_set_text(cancel_label, str_language_cancel[get_curr_language()]);
    lv_obj_add_style(cancel_label, &SMALL_SIZE, LV_PART_MAIN);
    lv_obj_align(cancel_label, LV_ALIGN_CENTER, 0, 0);

    /* 确认按钮。 */
    data->password_confirm_btn = lv_btn_create(data->password_panel);
    lv_obj_set_size(data->password_confirm_btn, 112, 38);
    lv_obj_align(data->password_confirm_btn, LV_ALIGN_BOTTOM_RIGHT, -12, 2);
    lv_obj_add_event_cb(data->password_confirm_btn, password_confirm_btn_cb, LV_EVENT_CLICKED, data);
    /* 确认按钮文字。 */
    confirm_label = lv_label_create(data->password_confirm_btn);
    lv_label_set_text(confirm_label, str_language_confirm[get_curr_language()]);
    lv_obj_add_style(confirm_label, &SMALL_SIZE, LV_PART_MAIN);
    lv_obj_align(confirm_label, LV_ALIGN_CENTER, 0, 0);

    /* 屏幕底部密码键盘。 */
    data->password_kb = lv_keyboard_create(data->password_modal_mask);
    lv_obj_set_width(data->password_kb, LV_PCT(100));
    lv_obj_set_height(data->password_kb, 220);
    lv_obj_align(data->password_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(data->password_kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(data->password_kb, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_style_border_width(data->password_kb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(data->password_kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(data->password_kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(data->password_kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(data->password_kb, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(data->password_kb, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_column(data->password_kb, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(data->password_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(data->password_kb, lv_color_hex(0x2A2A2A), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(data->password_kb, lv_color_hex(0x3A3A3A), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(data->password_kb, lv_color_hex(0x3A3A3A), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(data->password_kb, lv_color_hex(0x464646), LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(data->password_kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(data->password_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(data->password_kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(data->password_kb, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(data->password_kb, &lv_font_montserrat_28, LV_PART_ITEMS);
    lv_keyboard_set_mode(data->password_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(data->password_kb, data->password_ta);
    lv_obj_add_event_cb(data->password_kb, password_keyboard_event_cb, LV_EVENT_ALL, data);

    /* WiFi 列表容器。 */
    data->wifi_list = lv_list_create(data->container);
    lv_obj_set_width(data->wifi_list, H_RES);
    lv_obj_set_flex_grow(data->wifi_list, 1);
    lv_obj_set_style_margin_all(data->wifi_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(data->wifi_list, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(data->wifi_list, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_add_style(data->wifi_list, &NORMAL_SIZE, LV_PART_MAIN);
    lv_obj_set_scroll_dir(data->wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(data->wifi_list, LV_SCROLLBAR_MODE_AUTO);

    /* 初始化定时器为 NULL */
    data->scan_timer = NULL;
    data->connect_timer = NULL;
    data->switch_apply_timer = NULL;
    data->scan_count = 0;
    data->pending_scan_id = -1;
    data->connect_poll_count = 0;
    data->connecting_ssid[0] = '\0';
    data->switch_apply_pending = 0;
    data->switch_target_enabled = data->wifi_enabled;
    data->switch_ignore_event = 0;

    /* 初始化WiFi列表焦点选择相关变量 */
    data->current_wifi_index = -1;
    data->current_selected_btn = NULL;

    /* 保存 private_data */
    page_set_private_data(data);

    /* 注册按键处理 */
    set_current_page_handler(wifi_list_key_handler);

    /* 首次创建页面时，若 WiFi 已开启则主动扫描一次并提示。 */
    if (data->wifi_enabled) {
        start_wifi_scan(data);
    }
}

void page_wifi_list_destroy(void)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)page_get_private_data();
    cleanup_wifi_page_data(data);
}

void page_wifi_list_show(void)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)page_get_private_data();
    if (data == NULL || data->container == NULL) {
        return;
    }

    /* 每次进入页面同步真实 WiFi 开关状态，避免显示陈旧状态。 */
    sync_wifi_enabled_state(data);

    /* 重置焦点选择 */
    data->current_wifi_index = -1;
    data->current_selected_btn = NULL;

    /* 重新注册按键处理 */
    set_current_page_handler(wifi_list_key_handler);

    /* WiFi 已启用则开始扫描 */
    if (data->wifi_enabled) {
        start_wifi_scan(data);
    } else {
        stop_connect_timer(data);
        if (data->scan_timer != NULL) {
            lv_timer_del(data->scan_timer);
            data->scan_timer = NULL;
        }
        lv_obj_clean(data->wifi_list);
    }

    lv_obj_clear_flag(data->container, LV_OBJ_FLAG_HIDDEN);
}

void page_wifi_list_hide(void)
{
    page_wifi_list_data_t* data = (page_wifi_list_data_t*)page_get_private_data();
    if (data == NULL || data->container == NULL) {
        return;
    }

    cleanup_wifi_page_runtime(data);

    /* 取消按键处理注册 */
    set_current_page_handler(NULL);

    lv_obj_add_flag(data->container, LV_OBJ_FLAG_HIDDEN);
}

void page_wifi_list_update(void)
{
    /* no-op */
}

// #endregion
// #############################################################################
// ! #region 9. 调试与测试
// #############################################################################

// #endregion
