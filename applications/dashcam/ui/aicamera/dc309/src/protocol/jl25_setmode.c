/*
 * JL25xx 协议 /SetMode 命令组（协议 2.2 节）
 *
 *   /SetMode?Storage       - 进入存储模式（U 盘）
 *   /SetMode?Setup         - 进入设置模式
 *   /SetMode?PhotoCapture  - 进入拍照预览模式
 *   /SetMode?VideoCapture  - 进入录像模式
 *
 * 模式映射（设备 WORK_MODE_E -> 协议 Mode 字符串）：
 *   WORK_MODE_MOVIE/PHOTO -> Camera（可再切 PhotoCapture/VideoCapture）
 *   WORK_MODE_STORAGE     -> Storage
 *   WORK_MODE_BOOT        -> Setup
 *
 * 注意：设备当前处于 WORK_MODE_BOOT（开机默认）时，先通过
 * EVENT_MODEMNG_MODESWITCH 切到目标工作模式，再同步 RECORD_MODE 参数。
 * 实际拍照录像仍由 /Setup?Shutter 触发。
 */
#define DEBUG
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"

#define JL25_SETMODE_WAIT_MS 100
#define JL25_SETMODE_WAIT_CNT 50

static int jl25_setmode_switch_work_mode(WORK_MODE_E target)
{
    MESSAGE_S msg = { 0 };
    int i;

    if (MODEMNG_GetCurWorkMode() != (int32_t)target) {
        msg.topic = EVENT_MODEMNG_MODESWITCH;
        msg.arg1 = target;
        if (MODEMNG_SendMessage(&msg) != 0) {
            return -1;
        }
    }

    for (i = 0; i < JL25_SETMODE_WAIT_CNT; i++) {
        if (MODEMNG_GetCurWorkMode() == (int32_t)target && !MODEMNG_GetInProgress()) {
            return 0;
        }
        usleep(JL25_SETMODE_WAIT_MS * 1000);
    }
    return -1;
}

static int jl25_setmode_set_record_mode(int value)
{
    MESSAGE_S msg = { 0 };

    msg.topic = EVENT_MODEMNG_SETTING;
    msg.arg1 = PARAM_MENU_RECORD_MODE;
    msg.arg2 = value;
    return MODEMNG_SendMessage(&msg);
}

void jl25_setmode_handle(const char* query, int fd)
{
    if (strstr(query, "Storage") != NULL) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 setmode preview stop before storage fail\n");
        }
        /* 切 USB 存储模式需整机配合（usbctrl），APP 场景不用，回失败 */
        jl25_http_send_json(fd, "{\"Result\":-1}");
    } else if (strstr(query, "Setup") != NULL) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 setmode preview stop before setup fail\n");
        }
        jl25_http_send_json(fd, "{\"Result\":0,\"Mode\":\"Setup\"}");
    } else if (strstr(query, "PhotoCapture") != NULL) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 setmode preview stop before photo mode fail\n");
        }
        if (jl25_setmode_switch_work_mode(WORK_MODE_PHOTO) != 0) {
            MLOG_ERR("jl25 setmode switch to photo mode fail\n");
            jl25_http_send_json(fd, "{\"Result\":-1}");
            return;
        }
        /* 同步拍照语义，供后续协议查询和录像模式恢复使用。 */
        if (jl25_setmode_set_record_mode(0) != 0) {
            MLOG_ERR("jl25 setmode set photo record mode fail\n");
        }
        jl25_http_send_json(fd, "{\"Result\":0,\"Mode\":\"PhotoCapture\"}");
    } else if (strstr(query, "VideoCapture") != NULL) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 setmode preview stop before video mode fail\n");
        }
        if (jl25_setmode_switch_work_mode(WORK_MODE_MOVIE) != 0) {
            MLOG_ERR("jl25 setmode switch to video mode fail\n");
            jl25_http_send_json(fd, "{\"Result\":-1}");
            return;
        }
        if (jl25_setmode_set_record_mode(1) != 0) {
            MLOG_ERR("jl25 setmode set video record mode fail\n");
        }
        jl25_http_send_json(fd, "{\"Result\":0,\"Mode\":\"VideoCapture\"}");
    } else {
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", strlen("{\"Result\":-404}"));
    }
}
