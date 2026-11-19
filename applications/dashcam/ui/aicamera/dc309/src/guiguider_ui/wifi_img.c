#define DEBUG
// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#include "wifi_img.h"
#include "common/extract_thumbnail.h"
#include "common/takephoto.h"
#include "custom.h"
#include "filemng.h"
#include "lvgl.h"
#include "lvgl/src/libs/freetype/lv_freetype.h"
#include "mapi_ao.h"
#include "page_all.h"
#include "param_manager.h"
#include "ui_common.h"
#include "voiceplay.h"
#include "wifi_manager.h"
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// #endregion
// #############################################################################
// ! #region 2. 数据结构定义
// #############################################################################

// #endregion
// #############################################################################
// ! #region 3. 全局变量 &  函数声明
// #############################################################################
static lv_obj_t* s_img_wifi = NULL;
static lv_timer_t* wifi_update_timer_s = NULL; // 更新定时器

// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################

static void wifi_update_cb(lv_timer_t* t)
{
    int wifi_enabled = 0;
    int wifi_connected = 0;
    int wifi_signal = -1;
    int level = 0;

    wifi_manager_poll();
    param_manager_poll();
    wifi_enabled = param_manager_get(PARAM_ID_WIFI_ENABLED);
    wifi_connected = param_manager_get(PARAM_ID_WIFI_CONNECTED);
    wifi_signal = param_manager_get(PARAM_ID_WIFI_SIGNAL_DBM);
    level = wifi_level_from_dbm(wifi_signal);

    if (!lv_obj_is_valid(s_img_wifi)) {
        MLOG_ERR("wifi img invalid");
        return;
    }
    if (wifi_enabled == 0 || wifi_connected == 0 || level == 0) {
        show_image(s_img_wifi, "无网络.png");
    } else {
        lv_img_set_src(s_img_wifi, level == 1 ? &_wifi_1_RGB565A8_24x23 : level == 2 ? &_wifi_2_RGB565A8_24x23
                : level == 3                                                         ? &_wifi_3_RGB565A8_24x23
                                                                                     : &_wifi_4_RGB565A8_24x23);
    }
}

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################

int wifi_level_get(void)
{
    wifi_manager_poll();
    param_manager_poll();
    int dbm = param_manager_get(PARAM_ID_WIFI_SIGNAL_DBM);
    return wifi_level_from_dbm(dbm);
}

int wifi_level_from_dbm(int dbm)
{
    static int wifi_level = 0;
    if (dbm == -1)
        wifi_level = 0;
    else if (dbm >= -50)
        wifi_level = 4; // >=-50: 满格
    else if (dbm >= -60)
        wifi_level = 3; // -60~-50: 3格
    else if (dbm >= -70)
        wifi_level = 2; // -70~-60: 2格
    else if (dbm >= -80)
        wifi_level = 1; // -80~-70: 1格
    else if (dbm < -80)
        wifi_level = 1;
    return wifi_level;
}

void create_wifi_img(lv_obj_t* parent, int32_t x_ofs, int32_t y_ofs)
{
    MLOG_DBG("create_wifi_img");
    s_img_wifi = lv_img_create(parent);
    lv_obj_set_size(s_img_wifi, 24, 23);
    lv_obj_align(s_img_wifi, LV_ALIGN_TOP_RIGHT, x_ofs, y_ofs);
    lv_obj_set_style_pad_all(s_img_wifi, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(s_img_wifi, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(s_img_wifi, LV_OPA_COVER, LV_PART_MAIN);

    if (wifi_update_timer_s == NULL) {
        wifi_update_timer_s = lv_timer_create(wifi_update_cb, 1000, NULL);
        lv_timer_ready(wifi_update_timer_s);
    } else {
        MLOG_WARN("wifi timer already exist");
    }
}

void delete_wifi_img(void)
{
    MLOG_DBG("delete_wifi_img");
    if (lv_obj_is_valid(s_img_wifi)) {
        lv_obj_del(s_img_wifi);
        s_img_wifi = NULL;
    } else {
        MLOG_WARN("wifi img delete error,not exist");
    }
    if (wifi_update_timer_s != NULL) {
        lv_timer_del(wifi_update_timer_s);
        wifi_update_timer_s = NULL;
    } else {
        MLOG_WARN("wifi timer delete error,not exist");
    }
}

// #endregion
// #############################################################################
// ! #region 8. 初始化、去初始化、资源管理
// #############################################################################

// #endregion
