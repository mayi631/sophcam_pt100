/*
 * JL25xx 协议 /Misc 命令组（协议 2.1 节）
 *
 *   /Misc?AliveAck              - 心跳：APP 3 分钟内有命令则设备不关 WiFi AP
 *   /Misc?PowerOff              - 远程关机
 *   /Misc?GetCurrentWorkMode    - 获取当前工作模式
 *   /Misc?JpgSyncErrorAck       - 预览流同步错误通知
 *   /Misc?IsWifiPasswordValid   - 检查 WiFi 密码是否有效
 */
#define DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

#include "jl25_internal.h"
#include "jl25_misc.h"
#include "jl25_protocol.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"

/* 心跳看门狗（AliveAck / 任意命令刷新），到期由热点页面或本模块关闭热点。
 * 协议定义：APP 连上后 3 分钟无命令则设备关 WiFi AP。此处只维护时间戳，
 * 实际关闭动作由外部定时检查（JL25_AliveTimeoutCheck）。
 */
#include <pthread.h>
#include <time.h>
static time_t s_last_alive_time;
static pthread_mutex_t s_alive_mutex = PTHREAD_MUTEX_INITIALIZER;

void jl25_alive_refresh(void)
{
    pthread_mutex_lock(&s_alive_mutex);
    s_last_alive_time = time(NULL);
    pthread_mutex_unlock(&s_alive_mutex);
}

int32_t JL25_AliveTimeoutCheck(uint32_t timeout_sec)
{
    int32_t ret = 0;
    pthread_mutex_lock(&s_alive_mutex);
    if (s_last_alive_time != 0 && (uint32_t)(time(NULL) - s_last_alive_time) > timeout_sec) {
        ret = 1; /* 已超时 */
    }
    pthread_mutex_unlock(&s_alive_mutex);
    return ret;
}

void jl25_misc_handle(const char* query, int fd)
{
    jl25_alive_refresh();

    if (strstr(query, "AliveAck") != NULL) {
        jl25_http_send_json(fd, "{\"Result\":0}");
    } else if (strstr(query, "PowerOff") != NULL) {
        /* APP 强制关机：推送事件后延迟执行，让响应先发出 */
        JL25_PushEvent(JL25_EVENT_FORCE_POWEROFF, NULL);
        jl25_http_send_json(fd, "{\"Result\":0}");
        sleep(1);
        MLOG_ERR("jl25: PowerOff by APP\n");
        reboot(RB_POWER_OFF);
    } else if (strstr(query, "GetCurrentWorkMode") != NULL) {
        char resp[128];
        int mode = MODEMNG_GetCurWorkMode();
        snprintf(resp, sizeof(resp),
            "{\"Result\":0,\"Mode\":\"%s\"}",
            JL25_WorkModeToStr(mode));
        jl25_http_send_json(fd, resp);
    } else if (strstr(query, "JpgSyncErrorAck") != NULL) {
        /* APP 端 128KB 内找不到 FFD8/FFD9，通知流异常。当前无 MJPG
		 * 预览流实现，直接回成功 */
        jl25_http_send_json(fd, "{\"Result\":0}");
    } else if (strstr(query, "IsWifiPasswordValid") != NULL) {
        /* 设备侧不存在"无效密码"场景（AP 模式密码即配置），直接回 ok */
        jl25_http_send_json(fd, "{\"Result\":0,\"NeedReSet\":0}");
    } else if (strstr(query, "Info") != NULL || strstr(query, "GetInfo") != NULL) {
        /* 协议 2.1 节 Get System Info（/Misc?Info） */
        char resp[512];
        PARAM_WIFI_S WifiParam = { 0 };
        PARAM_GetWifiParam(&WifiParam);
        snprintf(resp, sizeof(resp),
            "{\"Result\":0,"
            "\"ProductName\":\"SOPHCAM\","
            "\"ModelName\":\"DC309\","
            "\"ChipID\":\"CV1842CP\","
            "\"ProtocolVer\":%d,"
            "\"WifiChip\":\"%s\"}",
            JL25_PROTOCOL_VER,
#ifdef WIFI_RTL8188
            "RTL8188FU");
#else
            "RTL8189FS");
#endif
        (void)WifiParam;
        jl25_http_send_json(fd, resp);
    } else {
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", strlen("{\"Result\":-404}"));
    }
}
