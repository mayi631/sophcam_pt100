/*
 * JL25xx 协议 /Setup 命令组（协议 2.4 节）
 *
 * 已实现：
 *   /Setup?Format                    - 格式化 SD 卡
 *   /Setup?Reset                     - 恢复出厂设置
 *   /Setup?<Param>=<value>           - 不支持（隐藏设置项）
 *   /Setup?GetBatteryStatus          - 电池状态
 *   /Setup?Shutter=N                 - 快门（0 停止录像 / 1 开始录像 / 2 拍照）
 *   /Setup?GetRecordingStatus        - 录制状态
 *   /Setup?GetTemperatureStatus      - 温度（设备无传感器，回 0）
 *   /Setup?GetAllSettingsList        - 全部设置（仅保留系统动作）
 *   /Setup?GetNightModeFlag          - 夜视模式标志（设备无 IR，固定 0）
 *   /Setup?GetGPS_Info / SetGPS_Info - GPS（设备无 GPS，回空）
 *
 * 未实现（设备无对应硬件/能力，回 Result:0 空操作）：
 *   GetZoomInfo/ZoomLevel、NightMode、FocusMode、MacroOn、
 *   GetMenuJson/GetDicJson、SPI_Update、GetPreviewFrame、
 *   GetCurrentTargetResolution
 *
 */
#define DEBUG
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "filemng.h"
#include "gaugemng.h"
#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "media_init.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"
#include "ui_common.h"

/* ==================== 设置项映射 ==================== */

/* Serialize APP start/stop commands while recorder is being rebuilt. */
static pthread_mutex_t s_jl25_capture_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_jl25_remote_recording;

int jl25_factory_reset(void)
{
    PARAM_MEDIA_COMM_S media_comm_param = { 0 };
    PARAM_CFG_S* param;
    uint32_t focus_pos;
    uint32_t focus_pos_lock;
    int ret;

    PARAM_GetMediaComm(&media_comm_param);
    focus_pos = media_comm_param.Record.ChnAttrs[0].FocusPos;
    focus_pos_lock = media_comm_param.Record.ChnAttrs[0].FocusPosLock;

    /* PARAM_CFG_S is about 300 KB; do not put it on the HTTP thread stack. */
    param = (PARAM_CFG_S*)calloc(1, sizeof(*param));
    if (param == NULL) {
        MLOG_ERR("jl25 factory reset alloc default param failed\n");
        return -1;
    }

    ret = PARAM_LoadFromDefBin(param);
    if (ret != 0) {
        MLOG_ERR("jl25 factory reset load default failed, ret=%d\n", ret);
        free(param);
        return ret;
    }

    /* Keep the calibrated lens position across the factory reset. */
    param->MediaComm.Record.ChnAttrs[0].FocusPos = focus_pos;
    param->MediaComm.Record.ChnAttrs[0].FocusPosLock = focus_pos_lock;

    ret = PARAM_SetParam(param);
    if (ret != 0) {
        MLOG_ERR("jl25 factory reset set default failed, ret=%d\n", ret);
        free(param);
        return ret;
    }

    ret = PARAM_SaveParam();
    if (ret != 0) {
        MLOG_ERR("jl25 factory reset save default failed, ret=%d\n", ret);
        free(param);
        return ret;
    }

    free(param);
    MLOG_INFO("jl25 factory reset completed\n");
    return 0;
}

static int jl25_capture_wait_mode_state(int recording)
{
    uint32_t mode_state = 0;
    int i;

    for (i = 0; i < 50; i++) {
        MODEMNG_GetModeState(&mode_state);
        if (recording) {
            if (mode_state == MEDIA_MOVIE_STATE_REC || mode_state == MEDIA_MOVIE_STATE_LAPSE_REC) {
                return 0;
            }
        } else if (mode_state != MEDIA_MOVIE_STATE_REC && mode_state != MEDIA_MOVIE_STATE_LAPSE_REC) {
            return 0;
        }
        usleep(100 * 1000);
    }
    return -1;
}

static int jl25_capture_send_event(uint32_t topic)
{
    MESSAGE_S msg = { 0 };

    msg.topic = topic;
    return MODEMNG_SendMessage(&msg);
}

static int jl25_capture_switch_mode(WORK_MODE_E target)
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

    for (i = 0; i < 100; i++) {
        if (MODEMNG_GetCurWorkMode() == (int32_t)target && !MODEMNG_GetInProgress()) {
            return 0;
        }
        usleep(100 * 1000);
    }
    return -1;
}

static int jl25_capture_count_photos(void)
{
    DIR* d;
    struct dirent* ent;
    int count = 0;

    d = opendir("/mnt/sd/DCIM/PHOTO");
    if (d == NULL) {
        return -1;
    }
    while ((ent = readdir(d)) != NULL) {
        char path[256];
        struct stat st;

        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/mnt/sd/DCIM/PHOTO/%s", ent->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            count++;
        }
    }
    closedir(d);
    return count;
}

static int jl25_capture_wait_photo_done(int before)
{
    int i;
    for (i = 0; i < 100; i++) {
        int after = jl25_capture_count_photos();

        if (after >= 0 && after > before) {
            return 0;
        }
        usleep(100 * 1000);
    }
    return -1;
}

int jl25_capture_start_record(void)
{
    uint32_t mode_state = 0;
    int ret = -1;

    pthread_mutex_lock(&s_jl25_capture_mutex);
    MODEMNG_GetModeState(&mode_state);
    if (mode_state == MEDIA_MOVIE_STATE_REC || mode_state == MEDIA_MOVIE_STATE_LAPSE_REC) {
        ret = 0;
        goto out;
    }
    if (jl25_preview_ensure_movie_mode() != 0) {
        goto out;
    }
    if (MODEMNG_GetCardState() != CARD_STATE_AVAILABLE) {
        goto out;
    }

    /*
     * The proprietary recorder keeps its muxer and input ring buffers across
     * normal record sessions.  After APP playback/reconnect, stale audio
     * timestamps can survive into the next file, causing muxer to rewrite
     * every audio DTS to last+1.  Recreate only the record service before a
     * new remote recording; the JL25 RTSP service and its preview callback
     * remain running throughout this operation.
     */
    MLOG_INFO("jl25 capture rebuild recorder before remote recording\n");
    if (MEDIA_RecordSerDeInit() != 0) {
        MLOG_ERR("jl25 capture recorder deinit failed\n");
        goto out;
    }
    if (MEDIA_RecordSerInit() != 0) {
        MLOG_ERR("jl25 capture recorder init failed\n");
        goto out;
    }

    if (jl25_capture_send_event(EVENT_MODEMNG_START_REC) != 0) {
        goto out;
    }
    if (jl25_capture_wait_mode_state(1) != 0) {
        goto out;
    }
    jl25_app_record_duration_start();
    s_jl25_remote_recording = 1;
    ret = 0;

out:
    pthread_mutex_unlock(&s_jl25_capture_mutex);
    return ret;
}

int jl25_capture_stop_record(void)
{
    uint32_t mode_state = 0;
    int ret = -1;

    pthread_mutex_lock(&s_jl25_capture_mutex);
    /* An explicit APP stop always relinquishes remote-record ownership. */
    s_jl25_remote_recording = 0;
    MODEMNG_GetModeState(&mode_state);
    if (mode_state != MEDIA_MOVIE_STATE_REC && mode_state != MEDIA_MOVIE_STATE_LAPSE_REC) {
        ret = 0;
        goto out;
    }
    if (jl25_capture_send_event(EVENT_MODEMNG_STOP_REC) != 0) {
        goto out;
    }
    if (jl25_capture_wait_mode_state(0) != 0) {
        goto out;
    }
    jl25_app_record_duration_stop();
    ret = 0;

out:
    pthread_mutex_unlock(&s_jl25_capture_mutex);
    return ret;
}

void jl25_capture_app_disconnected(void)
{
    int should_stop = 0;

    /* appsock client count has already been decremented by the caller. */
    if (jl25_appsock_has_client()) {
        return;
    }

    pthread_mutex_lock(&s_jl25_capture_mutex);
    if (s_jl25_remote_recording) {
        /* Claim the stop so simultaneous client teardown callbacks do not
         * send duplicate STOP_REC events. */
        s_jl25_remote_recording = 0;
        should_stop = 1;
    }
    pthread_mutex_unlock(&s_jl25_capture_mutex);

    if (should_stop) {
        MLOG_INFO("jl25 APP disconnected, stop remote recording\n");
        if (jl25_capture_stop_record() != 0) {
            MLOG_ERR("jl25 APP disconnect stop remote recording failed\n");
        }
    }
}

void jl25_capture_protocol_stopping(void)
{
    pthread_mutex_lock(&s_jl25_capture_mutex);
    if (s_jl25_remote_recording) {
        MLOG_INFO("jl25 protocol stopping, stop remote recording\n");
        s_jl25_remote_recording = 0;
        pthread_mutex_unlock(&s_jl25_capture_mutex);
        if (jl25_capture_stop_record() != 0) {
            MLOG_ERR("jl25 protocol stop remote recording failed\n");
        }
        return;
    }
    pthread_mutex_unlock(&s_jl25_capture_mutex);
}

int jl25_capture_take_photo(void)
{
    int32_t original_mode;
    int32_t ret = -1;
    int before;
    int switched_to_photo = 0;

    if (MODEMNG_GetCardState() != CARD_STATE_AVAILABLE) {
        return -1;
    }

    /*
	 * CONFIG_ENABLE_SNAP is disabled for dc309, so the movie-mode
	 * RECORD_SERVICE PIV worker does not exist. Run the normal photo
	 * mode path instead and keep this workaround local to the protocol
	 * adapter.
	 */
    original_mode = MODEMNG_GetCurWorkMode();
    if (original_mode != WORK_MODE_PHOTO) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 capture preview stop failed\n");
        }
        if (jl25_capture_switch_mode(WORK_MODE_PHOTO) != 0) {
            MLOG_ERR("jl25 capture switch to photo mode failed\n");
            return -1;
        }
        switched_to_photo = 1;
    }

    before = jl25_capture_count_photos();
    if (before < 0) {
        MLOG_ERR("jl25 capture cannot scan photo directory\n");
        goto restore_mode;
    }

    MLOG_INFO("jl25 capture photo start, before=%d\n", before);
    ret = jl25_capture_send_event(EVENT_MODEMNG_START_PIV);
    if (ret != 0) {
        MLOG_ERR("jl25 capture send photo event failed: %d\n", ret);
        goto restore_mode;
    }

    ret = jl25_capture_wait_photo_done(before);
    if (ret != 0) {
        MLOG_ERR("jl25 capture photo file was not created\n");
        goto restore_mode;
    }
    MLOG_INFO("jl25 capture photo complete\n");

restore_mode:
    if (switched_to_photo) {
        if (jl25_capture_switch_mode(WORK_MODE_MOVIE) != 0) {
            MLOG_ERR("jl25 capture restore movie mode failed\n");
            return -1;
        }
        if (original_mode == WORK_MODE_MOVIE && jl25_preview_start() != 0) {
            MLOG_ERR("jl25 capture restart preview failed\n");
        }
    }
    return ret;
}

int jl25_capture_is_recording(void)
{
    uint32_t mode_state = 0;

    MODEMNG_GetModeState(&mode_state);
    return (mode_state == MEDIA_MOVIE_STATE_REC || mode_state == MEDIA_MOVIE_STATE_LAPSE_REC) ? 1 : 0;
}

/* ==================== 命令实现 ==================== */

/* Format：通过模式层格式化并等待文件管理扫描完成。 */
static void jl25_setup_format(int fd)
{
    int32_t ret = jl25_storage_format_card();
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"Result\":%d}", (ret == 0) ? 0 : -1);
    jl25_http_send_json(fd, resp);
}

/* GetBatteryStatus（协议 2.4.4） */
static void jl25_setup_getbattery(int fd)
{
    int32_t pct = GAUGEMNG_GetPercentage();
    bool charging = false;
    GAUGEMNG_GetChargeState(&charging);

    /* 协议 Level: 0=Empty 1=Low 2=Normal 3=Full；充电时 | 0x80 */
    int level;
    if (pct > 80) {
        level = 3;
    } else if (pct > 40) {
        level = 2;
    } else if (pct > 15) {
        level = 1;
    } else {
        level = 0;
    }
    if (charging) {
        level |= 0x80;
    }

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"Result\":0,\"Level\":%d,\"WifiLow\":0,\"IRLEDLow\":0}",
        level);
    jl25_http_send_json(fd, resp);
}

/* Shutter（协议 2.4.5）：0 停止录像 1 开始录像 2 拍照 */
static void jl25_setup_shutter(int fd, const char* query)
{
    char val[16];
    if (jl25_query_get(query, "Shutter", val, sizeof(val)) == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    int act = atoi(val);
    int32_t ret;

    switch (act) {
    case 0:
        ret = jl25_capture_stop_record();
        break;
    case 1:
        ret = jl25_capture_start_record();
        break;
    case 2:
        ret = jl25_capture_take_photo();
        break;
    default:
        ret = -1;
        break;
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"Result\":%d}", (ret == 0) ? 0 : -1);
    jl25_http_send_json(fd, resp);
}

/* GetRecordingStatus（协议 2.4.6）。
 * 录像状态以 mode state 为准，录制时长设备侧无单独接口，返回 0。
 */
static void jl25_setup_getrecstatus(int fd)
{
    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"Result\":0,\"Time\":0,\"IsMP4Recording\":%d}",
        jl25_capture_is_recording());
    jl25_http_send_json(fd, resp);
}

/* GetAllSettingsList（协议 2.4.12） */
static void jl25_setup_getallsettings(int fd)
{
    jl25_http_send_json(fd, "{\"Result\":0,\"Format\":1}");
}

/* SetGPS_Info/GetGPS_Info：设备无 GPS，返回空/成功 */
static void jl25_setup_gps(int fd, const char* query)
{
    char resp[128];
    if (strstr(query, "SetGPS_Info") != NULL) {
        /* 接收但不存储（无 GPS 模块） */
        jl25_http_send_json(fd, "{\"Result\":0}");
    } else {
        snprintf(resp, sizeof(resp),
            "{\"Result\":0,\"Latitude\":\"\",\"Longitude\":\"\",\"Altitude\":\"\"}");
        jl25_http_send_json(fd, resp);
    }
}

/* Timestamp=1752731580-28800：设置系统时间（秒级时间戳-时区偏移） */
static void jl25_setup_timestamp(int fd, const char* query)
{
    char val[64];
    if (jl25_query_get(query, "Timestamp", val, sizeof(val)) == NULL) {
        jl25_http_send_json(fd, "{\"Result\":-1}");
        return;
    }
    /* 格式：<秒级时间戳><+/-偏移秒>，偏移可能带'-'，无'+'前缀 */
    long ts = strtol(val, NULL, 10);
    /* 时区部分：找 '-'/（协议示例 1752731580-28800），不影响本地时间设置 */
    struct timespec tsp;
    tsp.tv_sec = (time_t)ts;
    tsp.tv_nsec = 0;
    clock_settime(CLOCK_REALTIME, &tsp);

    char resp[64];
    snprintf(resp, sizeof(resp), "{\"Result\":0}");
    jl25_http_send_json(fd, resp);
}

/* ==================== 入口 ==================== */

void jl25_setup_handle(const char* query, int fd)
{
    /* 无参数的 /Setup? */
    if (query[0] == '\0') {
        jl25_http_send_json(fd, "{\"Result\":0}");
        return;
    }

    if (strstr(query, "Format") != NULL) {
        jl25_setup_format(fd);
    } else if (strstr(query, "Reset") != NULL) {
        jl25_http_send_json(fd, jl25_factory_reset() == 0 ? "{\"Result\":0}" : "{\"Result\":-1}");
    } else if (strstr(query, "GetBatteryStatus") != NULL) {
        jl25_setup_getbattery(fd);
    } else if (strstr(query, "Shutter") != NULL) {
        jl25_setup_shutter(fd, query);
    } else if (strstr(query, "GetRecordingStatus") != NULL) {
        jl25_setup_getrecstatus(fd);
    } else if (strstr(query, "GetTemperatureStatus") != NULL) {
        jl25_http_send_json(fd, "{\"Result\":0,\"Celsius\":25}");
    } else if (strstr(query, "GetAllSettingsList") != NULL) {
        jl25_setup_getallsettings(fd);
    } else if (strstr(query, "GetGPS_Info") != NULL || strstr(query, "SetGPS_Info") != NULL) {
        jl25_setup_gps(fd, query);
    } else if (strstr(query, "Timestamp=") != NULL || strstr(query, "Timestamp") != NULL) {
        jl25_setup_timestamp(fd, query);
    } else if (strstr(query, "GetNightModeFlag") != NULL) {
        jl25_http_send_json(fd, "{\"Result\":0,\"Flag\":0}");
    } else if (strstr(query, "NightMode") != NULL || strstr(query, "FocusMode") != NULL || strstr(query, "MacroOn") != NULL || strstr(query, "SPI_Update") != NULL || strstr(query, "GetCurrentTargetResolution") != NULL) {
        /* 设备无对应硬件，空操作成功 */
        jl25_http_send_json(fd, "{\"Result\":0}");
    } else if (strstr(query, "GetMenuJson") != NULL) {
        jl25_http_send_json(fd,
            "{\"Menu Description\":{\"Caption\":\"Settings\",\"Caption Trans Idx\":0},\"Result\":0}");
    } else if (strstr(query, "GetDicJson") != NULL) {
        jl25_http_send_json(fd,
            "{\"0\":\"Settings\"}");
    } else if (strstr(query, "GetPreviewFrame") != NULL) {
        /* 预览流暂未实现，保持 404 */
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
    } else {
        /* 尝试按 <Param>=<value> 设置项处理 */
        char name[64];
        char value[64];
        const char* eq = strchr(query, '=');
        if (eq != NULL && eq != query) {
            size_t nlen = (size_t)(eq - query);
            if (nlen >= sizeof(name)) {
                nlen = sizeof(name) - 1;
            }
            memcpy(name, query, nlen);
            name[nlen] = '\0';
            const char* v = eq + 1;
            const char* amp = strchr(v, '&');
            size_t vlen = (amp != NULL) ? (size_t)(amp - v) : strlen(v);
            if (vlen >= sizeof(value)) {
                vlen = sizeof(value) - 1;
            }
            memcpy(value, v, vlen);
            value[vlen] = '\0';

            /* 热点名称/密码不对 APP 暴露，也不接受修改。 */
            if (strcasecmp(name, "ESSID") == 0 || strcasecmp(name, "WifiKey") == 0 || strcasecmp(name, "SSID") == 0 || strcasecmp(name, "Password") == 0) {
                jl25_http_send_response(fd, 404, "Not Found", "text/html",
                    "{\"Result\":-404}", 15);
                return;
            }

            (void)name;
            (void)value;
        }
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
    }
}
