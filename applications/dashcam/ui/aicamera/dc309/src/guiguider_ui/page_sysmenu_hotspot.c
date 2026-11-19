/*
 * 手机APP连接（相机热点）页面
 *
 * 功能说明：
 *   手机连接相机自身发射的热点（AP 模式），再通过专门的 APP 下发协议命令，
 *   实现下载图片/视频、远程拍照录像、以及其他参数设置等功能。
 *
 * 热点页面负责 AP 开关和 JL25 协议服务生命周期；APP 连接成功后通过 TCP:6035
 * 回调显示连接成功提示。
 *
 * 注意：get_usr_fonts() 每次调用都会新建字体对象，本页面仅创建两种字号
 * （顶部标题 34 / 热点信息 24）各一次，之后复用同一指针，避免重复创建。
 */
#define DEBUG
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "jl25_internal.h"
#include "linux/input.h"
#include "lvgl.h"
#include "mlog.h"
#include "page_all.h"
#include "style_common.h"
#include "top_notice.h"
#include "ui_common.h"
#ifdef WIFI_ON
#include "wifi_manager.h"
/* hal_wifi_common.h 不在 UI 的 include 路径中，此处直接声明底层热点接口 */
int32_t WIFI_AP_Start(char* ifname, const HAL_WIFI_APMODE_CFG_S* pstApCfg);
int32_t WIFI_AP_Stop(void);
/* JL25xx APP 协议服务（src/protocol/，随热点启停） */
extern int32_t JL25_Start(void);
extern int32_t JL25_Stop(void);
extern int32_t JL25_IsRunning(void);
extern void JL25_SetAppConnectionCallback(void (*callback)(void));
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ==================== 页面全局对象 ==================== */
lv_obj_t* obj_sysMenu_Hotspot_s; // 底层窗口
extern lv_style_t ttf_font_16;

/* 热点开关后台执行线程（避免阻塞 UI 主线程导致卡顿） */
static pthread_t hs_work_thread;
static int hs_work_busy = 0; /* 0=空闲, 1=正在处理 */
static int hs_work_result = -1;
static int hs_work_enable = 0; /* 本次后台操作类型：1=正在开启, 0=正在关闭 */
static int hs_pending_exit = 0; /* 等待本次操作结束后自动退出页面 */
/* 控件句柄 */
static lv_obj_t* hs_cont_top; // 顶部栏
static lv_obj_t* hs_btn_back; // 返回按钮
static lv_obj_t* hs_title; // 标题
static lv_obj_t* hs_ssid_label; // 热点 WiFi 名称（右侧值 label）
static lv_obj_t* hs_pass_label; // 热点密码（右侧值 label）
static lv_obj_t* hs_switch; // 热点开关
static lv_obj_t* hs_status_icon; // 连接状态圆点
static lv_obj_t* hs_status_text; // 连接状态文字

/* 缓存的热点配置（用于界面显示与开关切换） */
static char hs_ssid[HAL_WIFI_SSID_LEN] = { 0 };
static char hs_password[HAL_WIFI_PASSWORD_LEN] = { 0 };

/* ==================== 事件回调 ==================== */

/* 前置声明：打开/关闭热点（定义在下方） */
static int hotspot_set_enabled(int enable);
/* 前置声明：启动一次后台开启/关闭操作（定义在下方） */
static int hotspot_start_work(int enable);

/* 跳转到系统设置页面（退出热点页面的唯一出口） */
static void hotspot_goto_setting_page(void)
{
    ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting,
        LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

/*
 * 返回系统设置（退出热点页面，热点开着则先关闭恢复正常模式）
 *
 * 约束：热点正在开启/关闭的过程中不允许退出页面，必须等到操作真正结束
 * （开启完成/开启失败、关闭完成/关闭失败）之后才允许退出。
 * 因此退出时如果热点处于开启状态，这里只发起异步关闭流程，由
 * hotspot_work_done() 在操作结束后再自动跳转。
 */
static void hotspot_exit_page(void)
{
#ifdef WIFI_ON
    /* 正在开启/关闭中：拦截退出，仅提示当前状态 */
    if (hs_work_busy) {
        if (hs_work_enable) {
            top_notice_show_for(str_language_hotspot_turning_on[get_curr_language()],
                TOP_NOTICE_TYPE_INFO, 3000);
        } else {
            top_notice_show_for(str_language_hotspot_turning_off[get_curr_language()],
                TOP_NOTICE_TYPE_WARNING, 3000);
        }
        return;
    }

    /* 仅关闭本页面启动的 AP，不要把 STA 的 WiFi 开关当成热点状态。 */
    if (JL25_IsRunning()) {
        hs_pending_exit = 1;
        if (hotspot_start_work(0) != 0) {
            /* 线程创建失败，无法等待关闭结果，直接退出 */
            hs_pending_exit = 0;
            hotspot_goto_setting_page();
        }
        return;
    }
#endif
    hotspot_goto_setting_page();
}

/* 返回系统设置 */
static void hotspot_back_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        hotspot_exit_page();
    }
}

/* 直接在 UI 层打开/关闭热点（AP 模式），不经过消息状态机。
 * 说明：WiFi 驱动(8189fs.ko)已由 wifi-daemon 加载，不能再走 WIFIMNG_Start
 * (其内部会 insmod 重新加载驱动而失败)。因此直接调用 WIFI_AP_Start/Stop
 * 配置并启动 hostapd，利用已加载的驱动建立热点。
 */
static int hotspot_set_enabled(int enable)
{
#ifdef WIFI_ON
    PARAM_WIFI_S WifiParam = { 0 };
    HAL_WIFI_APMODE_CFG_S apCfg = { 0 };
    int32_t s32Ret = 0;

    if (PARAM_GetWifiParam(&WifiParam) != 0) {
        MLOG_ERR("hotspot: PARAM_GetWifiParam fail\n");
        return -1;
    }

    if (enable) {
        /* 1. 先关闭 wifi-daemon 的 station 连接，释放 wlan0 */
        if (wifi_manager_get_status() == 1) {
            wifi_manager_set_enabled(0);
        }

        /* 2. 构造 AP 配置（SSID = 基础名 + MAC 后三字节，工厂多台设备开热点时可区分） */
        apCfg.bHideSSID = WifiParam.WifiCfg.unCfg.stApCfg.bHideSSID;
        apCfg.s32Channel = WifiParam.WifiCfg.unCfg.stApCfg.s32Channel;
        if (apCfg.s32Channel < 1 || apCfg.s32Channel > 165) {
            apCfg.s32Channel = 6;
        }
        jl25_make_ap_ssid(
            WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiSSID[0] != '\0'
                ? WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiSSID
                : "SOPHCAM_XXXX",
            apCfg.stCfg.szWiFiSSID, sizeof(apCfg.stCfg.szWiFiSSID));
        snprintf(apCfg.stCfg.szWiFiPassWord, sizeof(apCfg.stCfg.szWiFiPassWord), "%s",
            WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiPassWord[0] != '\0'
                ? WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiPassWord
                : "12345678");

        /* 3. 直接启动 hostapd + udhcpd 建立热点 */
        s32Ret = WIFI_AP_Start((char*)HAL_WIFI_INTERFACE_NAME, &apCfg);
        if (s32Ret != 0) {
            MLOG_ERR("hotspot: WIFI_AP_Start fail, ret=%d\n", s32Ret);
            return -1;
        }

        /* 4. 热点就绪后启动 APP 协议服务（HTTP:80/TCP:8222/TCP:6035/UDP:49142） */
        if (JL25_Start() != 0) {
            MLOG_ERR("hotspot: JL25 protocol start fail\n");
            WIFI_AP_Stop();
            return -1;
        }
    } else {
        /* 关闭热点：停止协议服务 + hostapd + udhcpd */
        JL25_Stop();
        s32Ret = WIFI_AP_Stop();
        if (s32Ret != 0) {
            MLOG_ERR("hotspot: WIFI_AP_Stop fail, ret=%d\n", s32Ret);
            return -1;
        }
    }

    return 0;
#else
    (void)enable;
    MLOG_ERR("hotspot: WIFI_ON not defined\n");
    return -1;
#endif
}

/* 读取当前热点配置并更新界面 */
static void hotspot_refresh_config(void)
{
    PARAM_WIFI_S WifiParam = { 0 };
    const char* ssid = "";
    const char* password = "";
    const char* status = str_language_on[get_curr_language()];
    lv_color_t color = lv_color_hex(0x07C160);
    int hotspot_enabled = 0;

    PARAM_GetWifiParam(&WifiParam);
#ifdef WIFI_ON
    /* WifiParam.Enable 是 STA 开关，热点状态以 JL25 AP 服务为准。 */
    hotspot_enabled = JL25_IsRunning();
#endif

    /* 仅支持 AP 模式 */
    if (WifiParam.WifiCfg.enMode == HAL_WIFI_MODE_AP) {
        ssid = WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiSSID;
        password = WifiParam.WifiCfg.unCfg.stApCfg.stCfg.szWiFiPassWord;
    }

    jl25_make_ap_ssid(ssid[0] != '\0' ? ssid : "SOPHCAM_XXXX",
        hs_ssid, sizeof(hs_ssid));
    if (password[0] != '\0') {
        snprintf(hs_password, sizeof(hs_password), "%s", password);
    } else {
        snprintf(hs_password, sizeof(hs_password), "12345678");
    }

    /* 更新 SSID / 密码显示 */
    /* create_info_row 用 lv_obj_set_style_text_font(local style)设了 24px 字体,
     * local style 优先级高于 lv_obj_add_style 添加的普通样式,故 ttf_font_22 不生效;
     * 改为直接从 ttf_font_22 取出 18px 字体,用 local style 覆盖字体属性。 */
    {
        lv_style_value_t font_val = { 0 };
        lv_font_t* font_22 = NULL;
        if (lv_style_get_prop(&ttf_font_22, LV_STYLE_TEXT_FONT, &font_val) == LV_STYLE_RES_FOUND) {
            font_22 = (lv_font_t*)font_val.ptr;
        }
        if (hs_ssid_label != NULL) {
            lv_label_set_text(hs_ssid_label, hs_ssid);
            if (font_22 != NULL) {
                lv_obj_set_style_text_font(hs_ssid_label, font_22, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
        if (hs_pass_label != NULL) {
            lv_label_set_text(hs_pass_label, hs_password);
            if (font_22 != NULL) {
                lv_obj_set_style_text_font(hs_pass_label, font_22, LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }

    /* 更新开关与状态显示 */
    if (hs_switch != NULL) {
        if (hotspot_enabled) {
            lv_obj_add_state(hs_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(hs_switch, LV_STATE_CHECKED);
        }
    }
    if (hs_status_icon != NULL) {
        if (hotspot_enabled) {
            color = lv_color_hex(0x07C160);
        } else {
            color = lv_color_hex(0x8A8A8A);
            status = str_language_off[get_curr_language()];
        }
        lv_obj_set_style_bg_color(hs_status_icon, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (hs_status_text != NULL) {
        lv_label_set_text(hs_status_text, status);
        if (hs_status_icon != NULL) {
            lv_obj_align_to(hs_status_icon, hs_status_text, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        }
    }
}

/* 热点开关后台工作线程：执行耗时的热点开启/关闭操作，避免阻塞 UI 主线程 */
static void hotspot_work_done(void* data);
static void* hotspot_work_thread(void* arg)
{
    int enable = (int)(intptr_t)arg;
    MLOG_DBG("hotspot work thread start, enable=%d\n", enable);
    hs_work_result = hotspot_set_enabled(enable);
    /* 回到 UI 主线程刷新状态 */
    lv_async_call(hotspot_work_done, (void*)(intptr_t)enable);
    return NULL;
}

/* 主线程回调：热点操作完成后更新界面 */
static void hotspot_work_done(void* data)
{
    int enable = (int)(intptr_t)data;
    int pending_exit = hs_pending_exit;
    int ret = 0;

    hs_work_busy = 0;
    ret = hs_work_result;

    if (hs_switch != NULL) {
        lv_obj_clear_state(hs_switch, LV_STATE_DISABLED);
        if (ret == 0) {
            if (enable) {
                lv_obj_add_state(hs_switch, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(hs_switch, LV_STATE_CHECKED);
            }
        } else {
            /* 操作失败，回退开关状态 */
            if (enable) {
                lv_obj_clear_state(hs_switch, LV_STATE_CHECKED);
            } else {
                lv_obj_add_state(hs_switch, LV_STATE_CHECKED);
            }
        }
    }

    if (ret == 0) {
        hotspot_refresh_config();
    }

    /* 用户在操作过程中请求过退出：此时操作已结束（成功或失败），执行退出 */
    if (pending_exit) {
        hs_pending_exit = 0;
        hotspot_goto_setting_page();
        return;
    }

    if (ret == 0) {
        if (enable) {
            top_notice_show_for(str_language_hotspot_turned_on[get_curr_language()], TOP_NOTICE_TYPE_SUCCESS, 3000);
        } else {
            top_notice_show_for(str_language_hotspot_turned_off[get_curr_language()], TOP_NOTICE_TYPE_WARNING, 3000);
        }
    } else {
        if (enable) {
            top_notice_show_for(str_language_hotspot_turn_on_failed[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 3000);
        } else {
            top_notice_show_for(str_language_hotspot_turn_off_failed[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 3000);
        }
    }
}

/* 启动一次热点开启/关闭操作（后台线程执行，避免阻塞 UI 主线程） */
static int hotspot_start_work(int enable)
{
    hs_work_enable = enable;
    hs_work_busy = 1;

    if (hs_switch != NULL) {
        lv_obj_add_state(hs_switch, LV_STATE_DISABLED); /* 防止重复点击 */
    }

    /* 先提示"正在开启/关闭"，再后台执行耗时操作 */
    if (enable) {
        MLOG_DBG("正在开启相机热点\n");
        top_notice_show_for(str_language_hotspot_turning_on[get_curr_language()], TOP_NOTICE_TYPE_INFO, 5000);
    } else {
        MLOG_DBG("正在关闭相机热点\n");
        top_notice_show_for(str_language_hotspot_turning_off[get_curr_language()], TOP_NOTICE_TYPE_WARNING, 5000);
    }

    if (pthread_create(&hs_work_thread, NULL, hotspot_work_thread, (void*)(intptr_t)enable) != 0) {
        MLOG_ERR("create hotspot work thread fail\n");
        hs_work_busy = 0;
        if (hs_switch != NULL) {
            lv_obj_clear_state(hs_switch, LV_STATE_DISABLED);
        }
        top_notice_show_for(str_language_operation_failed[get_curr_language()], TOP_NOTICE_TYPE_ERROR, 3000);
        return -1;
    }

    return 0;
}

/* 热点开关回调 */
static void hotspot_switch_event_handler(lv_event_t* e)
{
    lv_obj_t* sw = lv_event_get_target(e);
    int enable;

    /* 上一次操作未完成，或正在等待关闭后退出页面，忽略本次操作 */
    if (hs_work_busy || hs_pending_exit) {
        /* 回退开关到当前实际状态 */
        if (JL25_IsRunning()) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        return;
    }

    enable = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    hotspot_start_work(enable);
}

/* APP 通过 TCP:6035 建立长连接后显示成功提示。 */
static void hotspot_app_connected_notice(void)
{
    top_notice_show_for(str_language_connection_successful[get_curr_language()],
        TOP_NOTICE_TYPE_SUCCESS, 3000);
}

/* 手势：右滑返回系统设置 */
static void hotspot_gesture_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT) {
            hotspot_exit_page();
        }
    }
}

/* 按键处理：菜单键返回 */
static void hotspot_key_handler(int key_code, int key_value)
{
    if (key_value == 1 && key_code == KEY_MENU) {
        hotspot_exit_page();
    }
}

/* ==================== 界面构建 ==================== */

/*
 * 创建一个"信息行"：左侧图标 + 左侧标题 + 右侧值
 * parent   : 父容器
 * icon     : 图标字符（LV_SYMBOL_xxx），可为 NULL
 * name     : 行标题文本
 * value    : 行右侧值文本
 * font     : 文字字体（统一字体）
 * width    : 行宽度
 * value_out: 输出右侧值 label 句柄（用于后续更新文本），可为 NULL
 */
static lv_obj_t* create_info_row(lv_obj_t* parent, const char* icon,
    const char* name, const char* value, lv_font_t* font, lv_coord_t width,
    lv_obj_t** value_out)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, width, 56);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 图标 */
    if (icon != NULL) {
        lv_obj_t* ic = lv_label_create(row);
        lv_label_set_text(ic, icon);
        lv_obj_set_style_text_color(ic, lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(ic, &lv_font_montserratMedium_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    }

    /* 标题 */
    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_SCROLL); /* 过长滚动 */
    lv_obj_set_width(name_label, 140);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(name_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 52, 0);

    /* 右侧值 */
    lv_obj_t* value_label = lv_label_create(row);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_color(value_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(value_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_SCROLL);
    lv_obj_set_size(value_label, width - 208, 40);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -12, 8);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (value_out != NULL) {
        *value_out = value_label;
    }

    return row;
}

/* 构建"手机APP连接"页面 */
void sysMenu_Hotspot(lv_ui_t* ui)
{
    lv_font_t* hs_font;
    lv_font_t* hs_font_small;

    hs_pending_exit = 0;

    /* 顶部标题字体（仅创建一次，避免覆盖全局单例） */
    hs_font = get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE);
    if (hs_font == NULL) {
        hs_font = (lv_font_t*)&lv_font_SourceHanSerifSC_Regular_20;
    }
    /* 热点信息小号字体（24px）：缩小后 SSID/密码等长文本可完整显示 */
    hs_font_small = get_usr_fonts(ALI_PUHUITI_FONTPATH, 24);
    if (hs_font_small == NULL) {
        hs_font_small = hs_font;
    }

    /* 底层窗口 */
    obj_sysMenu_Hotspot_s = lv_obj_create(NULL);
    lv_obj_set_size(obj_sysMenu_Hotspot_s, 640, 480);
    lv_obj_set_scrollbar_mode(obj_sysMenu_Hotspot_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_sysMenu_Hotspot_s, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_sysMenu_Hotspot_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj_sysMenu_Hotspot_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(obj_sysMenu_Hotspot_s, "二维码.jpg");
    lv_obj_add_event_cb(obj_sysMenu_Hotspot_s, hotspot_gesture_event_handler, LV_EVENT_GESTURE, ui);

    /* ========== 顶部栏 ========== */
    hs_cont_top = lv_obj_create(obj_sysMenu_Hotspot_s);
    lv_obj_set_pos(hs_cont_top, 0, 0);
    lv_obj_set_size(hs_cont_top, 640, 60);
    lv_obj_set_scrollbar_mode(hs_cont_top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(hs_cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 返回按钮 */
    hs_btn_back = lv_button_create(hs_cont_top);
    lv_obj_set_pos(hs_btn_back, 4, 4);
    lv_obj_set_size(hs_btn_back, 100, 52);
    lv_obj_add_style(hs_btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(hs_btn_back, hotspot_back_event_handler, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(hs_btn_back);
    lv_label_set_text(label_back, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(label_back, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(label_back, LV_PCT(100));
    lv_obj_add_style(label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 标题 */
    hs_title = lv_label_create(hs_cont_top);
    lv_label_set_text(hs_title, str_language_phone_app_connection[get_curr_language()]);
    lv_obj_set_style_text_color(hs_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hs_title, hs_font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(hs_title, LV_ALIGN_CENTER, -10, 0);

    /* 顶部分割线 */
    lv_obj_t* up_line = lv_line_create(obj_sysMenu_Hotspot_s);
    static lv_point_precise_t points_line[] = { { 10, 60 }, { 640, 60 } };
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);

    /* ========== 热点信息卡片（左侧栏，右侧留给二维码） ========== */
    lv_obj_t* card_info = lv_obj_create(obj_sysMenu_Hotspot_s);
    lv_obj_set_pos(card_info, 20, 185); /* 二维码纵向中心约 y=270，卡片高 170，185+85=270 居中 */
    lv_obj_set_size(card_info, 370, 170);
    lv_obj_set_style_bg_opa(card_info, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card_info, lv_color_hex(0x1E1E1E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card_info, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(card_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card_info, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 卡片标题：状态圆点 + "相机热点" */
    lv_obj_t* card_title = lv_label_create(card_info);
    lv_label_set_text(card_title, str_language_camera_hotspot[get_curr_language()]);
    lv_label_set_long_mode(card_title, LV_LABEL_LONG_SCROLL); /* 过长滚动 */
    lv_obj_set_width(card_title, 190);
    lv_obj_set_style_text_color(card_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(card_title, hs_font_small, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(card_title, LV_ALIGN_TOP_LEFT, 16, 10);

    /* 状态文字（跟随圆点） */
    hs_status_text = lv_label_create(card_info);
    lv_label_set_text(hs_status_text, str_language_on[get_curr_language()]);
    lv_obj_set_style_text_color(hs_status_text, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hs_status_text, hs_font_small, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(hs_status_text, LV_ALIGN_TOP_RIGHT, -14, 12);
    lv_obj_update_layout(hs_status_text);
    /* 状态圆点 */
    hs_status_icon = lv_obj_create(card_info);
    lv_obj_set_size(hs_status_icon, 14, 14);
    lv_obj_set_style_radius(hs_status_icon, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(hs_status_icon, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(hs_status_icon, hs_status_text, LV_ALIGN_OUT_LEFT_MID, -8, 0);

    /* 分隔线 */
    lv_obj_t* info_line = lv_line_create(card_info);
    static lv_point_precise_t pts_info_line[] = { { 16, 40 }, { 354, 40 } };
    lv_line_set_points(info_line, pts_info_line, 2);
    lv_obj_set_style_line_width(info_line, 1, 0);
    lv_obj_set_style_line_color(info_line, lv_color_hex(0x333333), 0);

    /* WiFi 名称行 */
    lv_obj_t* row_ssid = create_info_row(card_info, LV_SYMBOL_WIFI, str_language_wifi_name[get_curr_language()], hs_ssid, hs_font_small, 370, &hs_ssid_label);
    lv_obj_align(row_ssid, LV_ALIGN_TOP_LEFT, 0, 44);
    /* 密码行 */
    lv_obj_t* row_pass = create_info_row(card_info, LV_SYMBOL_KEYBOARD, str_language_password[get_curr_language()], hs_password, hs_font_small, 370, &hs_pass_label);
    lv_obj_align(row_pass, LV_ALIGN_TOP_LEFT, 0, 100);

    /* ========== 二维码提示（二维码区域约 (400,175)~(595,365)，上方保持空白勿覆盖） ========== */
    lv_obj_t* qr_hint = lv_label_create(obj_sysMenu_Hotspot_s);
    lv_label_set_text(qr_hint, str_language_scan_to_download_app[get_curr_language()]);
    lv_label_set_long_mode(qr_hint, LV_LABEL_LONG_SCROLL); /* 过长滚动 */
    lv_obj_set_style_text_color(qr_hint, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(qr_hint, &ttf_font_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(qr_hint, 180);
    lv_obj_set_pos(qr_hint, 404, 378);
    lv_obj_set_style_text_align(qr_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ========== 底部引导提示 ========== */
    lv_obj_t* guide_label = lv_label_create(obj_sysMenu_Hotspot_s);
    lv_label_set_text(guide_label, str_language_hotspot_guide[get_curr_language()]);
    lv_obj_set_style_text_color(guide_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(guide_label, &ttf_font_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(guide_label, LV_LABEL_LONG_SCROLL); /* 过长滚动 */
    lv_obj_set_width(guide_label, 600);
    lv_obj_align(guide_label, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_text_align(guide_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* ========== 热点开关（右上角） ========== */
    hs_switch = lv_switch_create(hs_cont_top);
    lv_obj_set_size(hs_switch, 80, 40);
    lv_obj_set_style_bg_color(hs_switch, lv_color_hex(0x4A4A4A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(hs_switch, lv_color_hex(0x07C160), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_align(hs_switch, LV_ALIGN_TOP_RIGHT, -16, 10);
    lv_obj_add_event_cb(hs_switch, hotspot_switch_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    /* APP 连接提示由协议线程触发，top_notice 内部会切回 LVGL 线程。 */
    JL25_SetAppConnectionCallback(hotspot_app_connected_notice);

    /* 读取实际热点配置，刷新 SSID/密码/状态/开关 */
    hotspot_refresh_config();

    /* 注册按键处理 */
    set_current_page_handler(hotspot_key_handler);

    /* 更新布局 */
    lv_obj_update_layout(obj_sysMenu_Hotspot_s);
}
