#ifndef __PAGE_WIFI_LIST_H__
#define __PAGE_WIFI_LIST_H__

#include "gui_guider.h"
#include "lvgl.h"
#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_LIST_MAX_AP_COUNT 32

typedef struct {
    lv_obj_t* container;
    lv_obj_t* nav_bar;
    lv_obj_t* wifi_switch;
    lv_obj_t* refresh_btn;
    lv_obj_t* wifi_list;
    lv_obj_t* password_modal_mask;
    lv_obj_t* password_panel;
    lv_obj_t* password_ssid_label;
    lv_obj_t* password_ta;
    lv_obj_t* password_cancel_btn;
    lv_obj_t* password_confirm_btn;
    lv_obj_t* password_kb;
    lv_timer_t* scan_timer;
    lv_timer_t* connect_timer;
    lv_timer_t* switch_apply_timer;
    uint8_t wifi_enabled;
    uint8_t switch_apply_pending;
    uint8_t switch_target_enabled;
    uint8_t switch_ignore_event;
    int pending_scan_id;
    int connect_poll_count;
    int scan_count;
    char pending_ssid[WIFI_MANAGER_MAX_SSID_LEN];
    char connecting_ssid[WIFI_MANAGER_MAX_SSID_LEN];
    wifi_ap_info_t scan_results[WIFI_LIST_MAX_AP_COUNT];
    // WiFi列表焦点选择相关
    int current_wifi_index; // 当前选中的WiFi索引
    lv_obj_t* current_selected_btn; // 当前选中的按钮对象
} page_wifi_list_data_t;

extern lv_obj_t* sysMenu_WifiList_s;

void sysMenu_WifiList(lv_ui_t* ui);
// void page_wifi_list_create(void);
void page_wifi_list_destroy(void);
void page_wifi_list_show(void);
void page_wifi_list_hide(void);
void page_wifi_list_update(void);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_WIFI_LIST_H__ */
