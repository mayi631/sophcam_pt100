/*
 * JL25xx 协议命令分发
 *
 * 路由规则（协议 2 章节号 -> path）：
 *   /Misc   -> jl25_misc.c
 *   /SetMode-> jl25_setmode.c
 *   /Storage-> jl25_storage.c
 *   /Setup  -> jl25_setup.c
 *   /Pin    -> jl25_pin_handle（最小兼容返回）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jl25_internal.h"
#include "jl25_protocol.h"

/* 未知命令：404 */
static void jl25_send_404(int fd)
{
    jl25_http_send_response(fd, 404, "Not Found", "text/html",
        "{\"Result\":-404}", strlen("{\"Result\":-404}"));
}

static void jl25_pin_handle(const char* query, int fd)
{
    char val[32];

    if (strstr(query, "RequestPinCheck") != NULL) {
        jl25_http_send_json(fd, "{\"Result\":0,\"Key0\":0}");
        return;
    }

    if (strstr(query, "SendPinCheck") != NULL) {
        (void)jl25_query_get(query, "SendPinCheck", val, sizeof(val));
        jl25_http_send_json(fd, "{\"Result\":0}");
        return;
    }

    jl25_http_send_response(fd, 404, "Not Found", "text/html",
        "{\"Result\":-404}", strlen("{\"Result\":-404}"));
}

/* 参数名大小写不敏感比较（协议中 GetDirFileInfo/Getdirfileinfo 混用） */
static int jl25_strcaseeq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca += 32;
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 32;
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* 取 query 中首个命令名（到 '&' 或结尾） */
static void jl25_query_first_token(const char* query, char* token, size_t sz)
{
    const char* amp = strchr(query, '&');
    if (amp != NULL) {
        size_t len = (size_t)(amp - query);
        if (len >= sz) {
            len = sz - 1;
        }
        memcpy(token, query, len);
        token[len] = '\0';
    } else {
        snprintf(token, sz, "%s", query);
    }
}

/* 取 query 中参数 param= 的值（写调用方缓冲），找不到返回 NULL
 * 仅匹配首个出现的 param
 */
const char* jl25_query_get(const char* query, const char* param,
    char* val, size_t val_sz)
{
    size_t plen = strlen(param);
    const char* p = query;

    while ((p = strstr(p, param)) != NULL) {
        /* 需要是参数起点：p == query 或前一个字符是 '&' */
        if ((p == query || p[-1] == '&') && p[plen] == '=') {
            const char* v = p + plen + 1;
            const char* amp = strchr(v, '&');
            size_t len = (amp != NULL) ? (size_t)(amp - v) : strlen(v);
            if (len >= val_sz) {
                len = val_sz - 1;
            }
            memcpy(val, v, len);
            val[len] = '\0';
            return val;
        }
        p += plen;
    }
    return NULL;
}

void JL25_HandleRequest(const char* path, const char* query,
    const char* request, int fd)
{
    if (jl25_strcaseeq(path, "/cgi-bin/hisnet/getwifi.cgi") || jl25_strcaseeq(path, "/cgi-bin/hisnet/getwifi.cgi/") || jl25_strcaseeq(path, "/cgi-bin/Config.cgi") || jl25_strcaseeq(path, "/cgi-bin/Config.cgi/") || jl25_strcaseeq(path, "/cammenu.xml") || strncmp(path, "/thumb/", 7) == 0) {
        jl25_legacy_handle(path, query, fd);
    } else if (strncmp(path, "/app/", 5) == 0) {
        jl25_app_handle(path, query, request, fd);
    } else if (jl25_app_is_media_path(path)) {
        jl25_app_handle_media_path(path, request, fd);
    } else if (jl25_strcaseeq(path, "/Misc")) {
        jl25_misc_handle(query, fd);
    } else if (jl25_strcaseeq(path, "/SetMode")) {
        jl25_setmode_handle(query, fd);
    } else if (jl25_strcaseeq(path, "/Storage")) {
        jl25_storage_handle(query, fd);
    } else if (jl25_strcaseeq(path, "/Setup")) {
        jl25_setup_handle(query, fd);
    } else if (jl25_strcaseeq(path, "/Pin")) {
        jl25_pin_handle(query, fd);
    } else {
        jl25_send_404(fd);
    }
}

/* 设备 WORK_MODE_E -> 协议模式字符串（协议 2.1.3 GetCurrentWorkMode）
 * 设备开机默认 WORK_MODE_BOOT，映射为 "Camera"（APP 预期的主要模式）
 */
const char* JL25_WorkModeToStr(int mode)
{
    switch (mode) {
    case 2: /* WORK_MODE_PLAYBACK */
    case 9: /* WORK_MODE_BOOT */
        return "Camera";
    case 8: /* WORK_MODE_STORAGE */
        return "Storage";
    case 1: /* WORK_MODE_PHOTO */
        return "PhotoCapture";
    case 0: /* WORK_MODE_MOVIE */
        return "VideoCapture";
    default:
        return "Setup";
    }
}

int JL25_StrToWorkMode(const char* str)
{
    if (strcmp(str, "Camera") == 0) {
        return JL25_WORKMODE_CAMERA;
    }
    if (strcmp(str, "Storage") == 0) {
        return JL25_WORKMODE_STORAGE;
    }
    if (strcmp(str, "PhotoCapture") == 0) {
        return JL25_WORKMODE_PHOTOCAPTURE;
    }
    if (strcmp(str, "VideoCapture") == 0) {
        return JL25_WORKMODE_VIDEOCAPTURE;
    }
    if (strcmp(str, "Setup") == 0) {
        return JL25_WORKMODE_SETUP;
    }
    return -1;
}
