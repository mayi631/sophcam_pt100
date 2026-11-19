/*
 * 自动标定功能
 */
// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#include "caminfomng.h"
#include "common/takephoto.h"
#include "cvi_af.h"
#include "gui_guider.h"
#include "lvgl.h"
#include "page_all.h"
#include "ui_common.h"
#include "voiceplay.h"
// #endregion
// #############################################################################
// ! #region 3. 全局变量 &  函数声明
// #############################################################################
static lv_timer_t* calibration_timer = NULL;
int calibration_step = 0;
extern takephoto_callback_t g_takephoto_after_callback;
extern takephoto_callback_t g_takephoto_before_callback;
extern uint8_t is_start_video; //录像状态

// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################

// 标定录像时的focus pos
static void cal_focus_pos(void)
{
    static uint32_t focus_pos_sum = 0;
    static uint32_t focus_pos_cnt = 0;
    ISP_FOCUS_Q_INFO_S focus_info = { 0 };
    PARAM_MEDIA_COMM_S mediacomm = { 0 };
    cam_info_t cam_info = { 0 };
    CVI_S32 ret = 0;

    PARAM_GetMediaComm(&mediacomm);
    /* 锁定时无法标定 */
    if (mediacomm.Record.ChnAttrs[0].FocusPosLock) {
        MLOG_WARN("focus pos is locked");
        return;
    }

    ret = CVI_ISP_AFQueryFocusInfo(0, &focus_info);
    if (ret != CVI_SUCCESS) {
        MLOG_ERR("ISP_AFQueryFocusInfo failed with %#x\n", ret);
        return;
    }

    focus_pos_sum += focus_info.u16FocusPos;
    focus_pos_cnt++;
    mediacomm.Record.ChnAttrs[0].FocusPos = focus_pos_sum / focus_pos_cnt;
    PARAM_SetMediaComm(&mediacomm);
    PARAM_SetSaveFlg();

    cam_info.FocusPos = mediacomm.Record.ChnAttrs[0].FocusPos;
    ret = CAMINFOMNG_UpdateInfo(&cam_info);
    if (ret != APP_SUCCESS) {
        MLOG_ERR("CAMINFOMNG_UpdateInfo failed:%#x", ret);
    }

    MLOG_INFO("focus_pos_sum:%d focus_pos_cnt:%d FocusPos:%d", focus_pos_sum, focus_pos_cnt, mediacomm.Record.ChnAttrs[0].FocusPos);
}

static void calibration_timer_cb(lv_timer_t* timer)
{
    MESSAGE_S Msg = { 0 };

    switch (calibration_step) {
    case 0: // 跳转到拍照页面
        static bool is_first_init = true;
        if (is_first_init) {
            is_first_init = false;
            // 显示黑色背景色，盖掉开机logo。后续送流的时候，会盖掉背景色。
            system("devmem 0x0a094094 32 0x00010028");
        }
        Msg.topic = EVENT_MODEMNG_MODESWITCH;
        Msg.arg1 = WORK_MODE_PHOTO;
        MODEMNG_SendMessage(&Msg);
        ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
        calibration_step++;
        lv_timer_set_period(timer, 2000); // 第一次切换，多等两秒
        break;
    case 1: // 开始自动对焦
        int32_t sensor_type = takephoto_get_current_sensor_type();
        if (sensor_type == TAKEPHOTO_SENSOR_SELFIE) { //如果是自拍就切换摄像头
            Msg.topic = EVENT_MODEMNG_SETTING;
            Msg.arg1 = PARAM_MENU_SENSOR_SWITCH;
            MODEMNG_SendMessage(&Msg);
            lv_timer_set_period(timer, 2000); // 等待摄像头切
            return;
        }

        display_viewfinder();
        Msg.topic = EVENT_MODEMNG_FOCUS;
        MODEMNG_SendMessage(&Msg);
        calibration_step++;
        lv_timer_set_period(timer, 2500); // 对焦间隔2.5秒
        break;
    case 2: // 后摄像头拍3张
        cancel_viewfinder(); // 取消对焦框
        cal_focus_pos(); //锁定焦距
        set_shootmode(1); //设置3连拍
        if (ui_common_cardstatus()) {
            if (ui_event_type == EVT_SDCARD_SPACE_FULL) {
                // 提示用户SD卡空间已满
                sd_card_status_label();
                return;
            }
            // 执行自定义的拍照后处理逻辑
            if (g_takephoto_after_callback != NULL) {
                g_takephoto_after_callback();
            }
        }
        calibration_step++;
        lv_timer_set_period(timer, 1200); // 拍照间隔2秒
        break;

    case 3: // 切换到前摄像头
        Msg.topic = EVENT_MODEMNG_SETTING;
        Msg.arg1 = PARAM_MENU_SENSOR_SWITCH;
        MODEMNG_SendMessage(&Msg);
        calibration_step++;
        lv_timer_set_period(timer, 2000); // 等待摄像头切换
        break;
    case 4: // 前摄像头拍3张
        set_shootmode(1);
        if (ui_common_cardstatus()) {
            if (ui_event_type == EVT_SDCARD_SPACE_FULL) {
                // 提示用户SD卡空间已满
                sd_card_status_label();
                return;
            }
            // 执行自定义的拍照后处理逻辑
            if (g_takephoto_after_callback != NULL) {
                g_takephoto_after_callback();
            }
        }
        calibration_step++;
        lv_timer_set_period(timer, 1200); // 拍照间隔2秒
        break;
    case 5: // 切换到后摄像头
        Msg.topic = EVENT_MODEMNG_SETTING;
        Msg.arg1 = PARAM_MENU_SENSOR_SWITCH;
        MODEMNG_SendMessage(&Msg);
        calibration_step++;
        lv_timer_set_period(timer, 3500); // 等待摄像头切换完成
        break;

    case 6: // 切换到录像页面
        Msg.topic = EVENT_MODEMNG_MODESWITCH;
        Msg.arg1 = WORK_MODE_MOVIE;
        MODEMNG_SendMessage(&Msg);
        ui_load_scr_animation(&g_ui, &obj_vedio_s, 1, &g_ui.screenHomePhoto_del, Home_Vedio,
            LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
        calibration_step = ZOOM_RADIO_MAX + 1;
        lv_timer_set_period(timer, 2000); // 等待页面切换
        break;
    case 17: //开始测试缩放功能 缩放16倍
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
        // 更新UI
        update_zoom_bar(calibration_step - ZOOM_RADIO_MAX);
        uint32_t new_level = get_zoom_level();
        // 设置放大比例
        Msg.topic = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
        Msg.arg1 = 0;
        snprintf((char*)Msg.aszPayload, 3, "%d", new_level);
        MODEMNG_SendMessage(&Msg);
        calibration_step++;
        lv_timer_set_period(timer, 300); //缩放间隔300ms
        break;
    case 33: // 开始录像
        Msg.topic = EVENT_MODEMNG_START_REC;
        MODEMNG_SendMessage(&Msg);
        is_start_video = VEDIO_START; //开始录像
        // 开始录像时，关闭音频输出
        UI_VOICEPLAY_DeInit(NULL);
        calibration_step++;
        lv_timer_set_period(timer, 4000); // 录制间隔4秒
        break;

    case 34: // 停止录像
        is_start_video = VEDIO_STOP; //停止录像
        Msg.topic = EVENT_MODEMNG_STOP_REC;
        MODEMNG_SendMessage(&Msg);
        // 停止录像时，开启音频输出
        UI_VOICEPLAY_Init();
        calibration_step++;
        lv_timer_set_period(timer, 500); //停止录像之后500ms删除定时器
        break;
    case 35:
        int ret = MAPI_SUCCESS;
        MAPI_AO_HANDLE_T ao_hdl = NULL;
        show_power_long_press_image();
        disable_touch_events(); //延时倒计时，禁用触摸
        disable_hardware_input_device(0);
        disable_hardware_input_device(1);
        ret = MAPI_AO_GetHandle(&ao_hdl);
        if (ret == MAPI_SUCCESS) {
            MAPI_AO_SetAmplifier(ao_hdl, CVI_FALSE);
        }
        lv_timer_del(calibration_timer);
        calibration_timer = NULL;
        break;
    }
}

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################

void auto_calibration_start(void)
{
    calibration_timer = lv_timer_create(calibration_timer_cb, 1000, NULL);
    calibration_step = 0;
}

// #endregion
// #endif
