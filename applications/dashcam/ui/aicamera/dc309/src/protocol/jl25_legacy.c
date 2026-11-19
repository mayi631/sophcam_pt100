/*
 * Legacy Hisnet/Netctrl CGI compatibility.
 *
 * Some phone APP versions discover the camera through the old CGI
 * interface instead of the JL25xx paths.  Keep this adapter in the
 * self-contained JL25 HTTP server because CONFIG_APPLICATION_NET is off
 * for this product and the original thttpd/netctrl service is absent.
 */
#define DEBUG
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "hal_wifi.h"
#include "jl25_internal.h"
#include "jl25_misc.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"

#define JL25_LEGACY_VERSION "CVITEK20210901"
#define JL25_LEGACY_FILE_MAX 512
#define JL25_LEGACY_FILE_BODY_MAX (512 * 1024)

static char s_legacy_camid[16] = "front";

typedef struct {
    char name[128];
    char path[256];
    time_t ctime;
    off_t size;
} jl25_legacy_file_t;

static int jl25_legacy_caseeq(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * WiFi credentials are deliberately not an APP setting on dc309.
 * Keep this check broader than the two documented legacy property names:
 * different APP releases use aliases such as ESSID, WifiKey and wifipwd.
 */
static int jl25_legacy_is_wifi_setting(const char* text)
{
    char lower[160];
    size_t i;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; text[i] != '\0' && i + 1 < sizeof(lower); i++) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        lower[i] = c;
    }
    lower[i] = '\0';

    return strstr(lower, "wifi") != NULL || strstr(lower, "ssid") != NULL || strstr(lower, "essid") != NULL || strstr(lower, "cryptokey") != NULL || strstr(lower, "wifikey") != NULL;
}

static int jl25_legacy_hex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Query values from old APPs are normally URL encoded. */
static void jl25_legacy_url_decode(const char* src, size_t src_len,
    char* dst, size_t dst_sz)
{
    size_t i;
    size_t out = 0;

    if (dst_sz == 0) {
        return;
    }
    for (i = 0; i < src_len && out + 1 < dst_sz; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int hi = jl25_legacy_hex(src[i + 1]);
            int lo = jl25_legacy_hex(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        dst[out++] = (src[i] == '+') ? ' ' : src[i];
    }
    dst[out] = '\0';
}

static const char* jl25_legacy_query_get(const char* query, const char* name,
    char* value, size_t value_sz)
{
    const char* p = query;
    size_t name_len = strlen(name);

    while (*p != '\0') {
        const char* item_end = strchr(p, '&');
        const char* eq = memchr(p, '=', item_end != NULL ? (size_t)(item_end - p) : strlen(p));
        if (eq != NULL && (size_t)(eq - p) == name_len && strncmp(p, name, name_len) == 0) {
            const char* value_start = eq + 1;
            size_t value_len = item_end != NULL ? (size_t)(item_end - value_start) : strlen(value_start);
            jl25_legacy_url_decode(value_start, value_len,
                value, value_sz);
            return value;
        }
        if (item_end == NULL) {
            break;
        }
        p = item_end + 1;
    }
    return NULL;
}

static int jl25_legacy_send_text(int fd, const char* body)
{
    return jl25_http_send_response(fd, 200, "OK",
        "text/plain; charset=utf-8",
        body, strlen(body));
}

static int jl25_legacy_send_status(int fd, int result)
{
    char body[64];
    snprintf(body, sizeof(body), "%d\n%s\n",
        result, result == 0 ? "OK" : "Fail");
    return jl25_legacy_send_text(fd, body);
}

/* Report WiFi credential APIs as unsupported at HTTP level as well. */
static int jl25_legacy_send_wifi_unsupported(int fd)
{
    static const char body[] = "-1\nFail\n";

    return jl25_http_send_response(fd, 404, "Not Found",
        "text/plain; charset=utf-8",
        body, sizeof(body) - 1);
}

static int jl25_legacy_send_unsupported(int fd)
{
    static const char body[] = "{\"Result\":-404}";

    return jl25_http_send_response(fd, 404, "Not Found", "text/html",
        body, sizeof(body) - 1);
}

static int jl25_legacy_send_property(int fd, const char* property,
    const char* value, int trailing_status)
{
    char body[512];
    int len;

    if (property == NULL || value == NULL) {
        return jl25_legacy_send_status(fd, -1);
    }
    len = snprintf(body, sizeof(body), "0\nOK\n%s=%s\n",
        property, value);
    if (trailing_status && len > 0 && (size_t)len < sizeof(body) - 16) {
        len += snprintf(body + len, sizeof(body) - (size_t)len,
            "0\nOK\n");
    }
    return jl25_http_send_response(fd, 200, "OK",
        "text/plain; charset=utf-8",
        body, (size_t)len);
}

static int jl25_legacy_get_mac(char* mac, size_t mac_sz)
{
    char path[96];
    int fd;
    ssize_t n;

    snprintf(path, sizeof(path), "/sys/class/net/%s/address",
        HAL_WIFI_INTERFACE_NAME);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    n = read(fd, mac, mac_sz - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    mac[n] = '\0';
    while (n > 0 && (mac[n - 1] == '\n' || mac[n - 1] == '\r' || mac[n - 1] == ' ' || mac[n - 1] == '\t')) {
        mac[--n] = '\0';
    }
    return mac[0] != '\0' ? 0 : -1;
}

static int jl25_legacy_card_available(void)
{
    return MODEMNG_GetCardState() == CARD_STATE_AVAILABLE;
}

static int jl25_legacy_file_cmp_desc(const void* a, const void* b)
{
    const jl25_legacy_file_t* fa = (const jl25_legacy_file_t*)a;
    const jl25_legacy_file_t* fb = (const jl25_legacy_file_t*)b;

    if (fa->ctime > fb->ctime) {
        return -1;
    }
    if (fa->ctime < fb->ctime) {
        return 1;
    }
    return strcmp(fa->name, fb->name);
}

static int jl25_legacy_file_cmp_asc(const void* a, const void* b)
{
    return -jl25_legacy_file_cmp_desc(a, b);
}

static int jl25_legacy_collect_files(const char* dir,
    jl25_legacy_file_t* files,
    int max_files)
{
    DIR* dp;
    struct dirent* ent;
    int count = 0;

    dp = opendir(dir);
    if (dp == NULL) {
        MLOG_ERR("jl25 legacy dir open fail, path=[%s]\n", dir);
        return 0;
    }
    while ((ent = readdir(dp)) != NULL && count < max_files) {
        char path[256];
        struct stat st;

        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        snprintf(files[count].name, sizeof(files[count].name), "%s",
            ent->d_name);
        snprintf(files[count].path, sizeof(files[count].path), "%s", path);
        files[count].ctime = st.st_ctime;
        files[count].size = st.st_size;
        count++;
    }
    closedir(dp);
    return count;
}

static void jl25_legacy_xml_escape(const char* src, char* dst,
    size_t dst_sz)
{
    size_t out = 0;

    while (*src != '\0' && out + 1 < dst_sz) {
        const char* replacement = NULL;
        switch (*src) {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        case '\'':
            replacement = "&apos;";
            break;
        default:
            dst[out++] = *src++;
            continue;
        }
        size_t len = strlen(replacement);
        if (out + len >= dst_sz) {
            break;
        }
        memcpy(dst + out, replacement, len);
        out += len;
        src++;
    }
    dst[out] = '\0';
}

static void jl25_legacy_append_xml(char* body, size_t body_sz,
    size_t* offset, const char* format, ...)
{
    va_list ap;
    int n;
    size_t avail;

    if (*offset >= body_sz) {
        return;
    }
    avail = body_sz - *offset;
    va_start(ap, format);
    n = vsnprintf(body + *offset, avail, format, ap);
    va_end(ap);
    if (n > 0) {
        *offset += (size_t)n < avail ? (size_t)n : avail - 1;
    }
}

static void jl25_legacy_get_file_dir(int fd, const char* property,
    const char* query)
{
    const char* dir_path = NULL;
    char root[32];
    char value[32];
    int start = 0;
    int backward = 0;
    jl25_legacy_file_t* files;
    int file_count;
    int first;
    int last;
    char* body;
    size_t offset = 0;
    int i;

    if (jl25_legacy_caseeq(property, "Normal") || jl25_legacy_caseeq(property, "Normal_rear")) {
        dir_path = "/mnt/sd/DCIM/MOVIE";
        snprintf(root, sizeof(root), "%s", property);
    } else if (jl25_legacy_caseeq(property, "Photo") || jl25_legacy_caseeq(property, "Photo_rear") || jl25_legacy_caseeq(property, "Event") || jl25_legacy_caseeq(property, "Event_rear")) {
        dir_path = "/mnt/sd/DCIM/PHOTO";
        snprintf(root, sizeof(root), "%s", property);
    } else if (jl25_legacy_caseeq(property, "ALL")) {
        dir_path = NULL;
        snprintf(root, sizeof(root), "ALL");
    } else {
        jl25_legacy_send_status(fd, -1);
        return;
    }

    if (jl25_storage_access_begin() != 0) {
        jl25_legacy_send_status(fd, -1);
        return;
    }

    if (jl25_legacy_query_get(query, "from", value, sizeof(value)) != NULL) {
        start = atoi(value);
    }
    if (jl25_legacy_query_get(query, "backward", value, sizeof(value)) != NULL) {
        backward = atoi(value) != 0;
    }
    if (start < 0) {
        start = 0;
    }

    files = (jl25_legacy_file_t*)calloc(JL25_LEGACY_FILE_MAX,
        sizeof(*files));
    if (files == NULL) {
        jl25_storage_access_end();
        jl25_legacy_send_status(fd, -1);
        return;
    }
    if (dir_path != NULL) {
        file_count = jl25_legacy_collect_files(dir_path, files,
            JL25_LEGACY_FILE_MAX);
    } else {
        file_count = jl25_legacy_collect_files("/mnt/sd/DCIM/MOVIE",
            files,
            JL25_LEGACY_FILE_MAX);
        if (file_count < JL25_LEGACY_FILE_MAX) {
            file_count += jl25_legacy_collect_files(
                "/mnt/sd/DCIM/PHOTO", files + file_count,
                JL25_LEGACY_FILE_MAX - file_count);
        }
    }
    qsort(files, file_count, sizeof(*files),
        backward ? jl25_legacy_file_cmp_desc : jl25_legacy_file_cmp_asc);

    first = start < file_count ? start : file_count;
    last = file_count;

    body = (char*)malloc(JL25_LEGACY_FILE_BODY_MAX);
    if (body == NULL) {
        free(files);
        jl25_storage_access_end();
        jl25_legacy_send_status(fd, -1);
        return;
    }
    jl25_legacy_append_xml(body, JL25_LEGACY_FILE_BODY_MAX, &offset,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\r\n"
        "<%s>\r\n",
        root);
    for (i = first; i < last; i++) {
        char path_esc[512];
        char format[32] = "";
        char time_str[32];
        struct tm tmv;
        const char* dot;

        jl25_legacy_xml_escape(files[i].path, path_esc,
            sizeof(path_esc));
        dot = strrchr(files[i].name, '.');
        if (dot != NULL) {
            snprintf(format, sizeof(format), "%s", dot + 1);
        }
        localtime_r(&files[i].ctime, &tmv);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
            &tmv);
        jl25_legacy_append_xml(
            body, JL25_LEGACY_FILE_BODY_MAX, &offset,
            "<file>\r\n"
            "<name>%s</name>\r\n"
            "<format>%s</format>\r\n"
            "<size>%lld</size>\r\n"
            "<attr>RW</attr>\r\n"
            "<time>%s</time>\r\n"
            "</file>\r\n",
            path_esc, format, (long long)files[i].size,
            time_str);
    }
    jl25_legacy_append_xml(body, JL25_LEGACY_FILE_BODY_MAX, &offset,
        "<amount>%d</amount>\r\n</%s>\r\n",
        file_count, root);
    MLOG_INFO("jl25 legacy dir property=[%s] total=%d from=%d returned=%d first=[%s]\n",
        property, file_count, first, last - first,
        (file_count > 0 && first < file_count) ? files[first].path : "");
    jl25_http_send_response(fd, 200, "OK",
        "text/xml; charset=utf-8", body, offset);
    free(body);
    free(files);
    jl25_storage_access_end();
}

static void jl25_legacy_get_card_info(int fd, const char* property)
{
    struct statvfs vfs;
    char value[64];
    const char* status = "NONE";
    unsigned long long total_gb = 0;
    unsigned long long free_gb = 0;

    if (jl25_storage_access_begin() != 0) {
        jl25_legacy_send_status(fd, -1);
        return;
    }

    if (jl25_legacy_card_available()) {
        status = "NORMAL";
        if (statvfs("/mnt/sd", &vfs) == 0) {
            unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
            unsigned long long free_bytes = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
            total_gb = total >> 30;
            free_gb = free_bytes >> 30;
            if (total_gb == 0) {
                total_gb = 1;
            }
        }
    }

    if (jl25_legacy_caseeq(property, "Camera.Menu.SDInfo")) {
        char body[256];
        snprintf(body, sizeof(body),
            "0\nOK\nGet SD Status\nSD0INFO=1\n"
            "Camera.Menu.SDInfo=%s\n",
            jl25_legacy_card_available() ? "ON" : "OFF");
        jl25_legacy_send_text(fd, body);
        jl25_storage_access_end();
        return;
    }

    if (strstr(property, "CardInfo.CardStatus") != NULL) {
        jl25_legacy_send_property(fd, property, status, 0);
        jl25_storage_access_end();
        return;
    }
    if (strstr(property, "CardInfo.total") != NULL) {
        snprintf(value, sizeof(value), " %lluG", total_gb);
        jl25_legacy_send_property(fd, property, value, 0);
        jl25_storage_access_end();
        return;
    }
    if (strstr(property, "CardInfo.remain") != NULL) {
        snprintf(value, sizeof(value), " %lluG", free_gb);
        jl25_legacy_send_property(fd, property, value, 0);
        jl25_storage_access_end();
        return;
    }
    jl25_legacy_send_property(fd, property,
        jl25_legacy_card_available() ? "ON" : "OFF", 0);
    jl25_storage_access_end();
}

static int jl25_legacy_handle_video_action(const char* value)
{
    if (jl25_legacy_caseeq(value, "recordon") || jl25_legacy_caseeq(value, "startrecord") || jl25_legacy_caseeq(value, "on") || jl25_legacy_caseeq(value, "1")) {
        return jl25_capture_start_record();
    }
    if (jl25_legacy_caseeq(value, "recordoff") || jl25_legacy_caseeq(value, "stoprecord") || jl25_legacy_caseeq(value, "off") || jl25_legacy_caseeq(value, "0")) {
        return jl25_capture_stop_record();
    }
    if (jl25_legacy_caseeq(value, "capture") || jl25_legacy_caseeq(value, "takephoto") || jl25_legacy_caseeq(value, "snapshot") || jl25_legacy_caseeq(value, "2")) {
        return jl25_capture_take_photo();
    }
    return -1;
}

static void jl25_legacy_get_property(int fd, const char* property)
{
    PARAM_MENU_S menu = { 0 };
    char value[128];
    char mac[32];

    if (jl25_legacy_is_wifi_setting(property)) {
        /* WiFi identity is intentionally not exposed to the APP. */
        /*
		 * Return HTTP 404, not HTTP 200 with a failure body.  APP versions
		 * commonly use the HTTP status to decide whether to show WiFi settings.
		 */
        MLOG_INFO("jl25 legacy WiFi property hidden property=[%s] response=404\n",
            property);
        jl25_legacy_send_wifi_unsupported(fd);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.FWVersion") || jl25_legacy_caseeq(property, "Camera.Menu.FWversion")) {
        jl25_legacy_send_property(fd, property, JL25_LEGACY_VERSION, 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.menu.UUID")) {
        if (jl25_legacy_get_mac(mac, sizeof(mac)) != 0) {
            snprintf(mac, sizeof(mac), "00:00:00:00:00:00");
        }
        jl25_legacy_send_property(fd, property, mac, 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.devid")) {
        jl25_legacy_send_status(fd, 0);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.*")) {
        const char* sd = jl25_legacy_card_available() ? "ON" : "OFF";
        char body[512];
        snprintf(body, sizeof(body),
            "0\nOK\n"
            "Camera.Menu.FWversion=%s\n"
            "Camera.Menu.menuXML=ON\n"
            "Camera.Menu.SDInfo=%s\n"
            "Camera.Menu.DeviceCapability=OFF\n"
            "0\nOK\n",
            JL25_LEGACY_VERSION, sd);
        jl25_legacy_send_text(fd, body);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.VideoresCapability") || jl25_legacy_caseeq(property, "Camera.Menu.DeviceCapability")) {
        jl25_legacy_send_property(fd, property, "OFF", 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.otaversion")) {
        jl25_legacy_send_property(fd, property, JL25_LEGACY_VERSION, 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Battery.Level")) {
        snprintf(value, sizeof(value), "100");
        jl25_legacy_send_property(fd, property, value, 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Menu.SDInfo") || strstr(property, "Camera.Menu.CardInfo.") != NULL) {
        jl25_legacy_get_card_info(fd, property);
        return;
    }
    if (strstr(property, "Preview.MJPEG.status.") != NULL) {
        uint32_t mode_state = 0;
        const char* status = "Standy";

        MODEMNG_GetModeState(&mode_state);
        if (mode_state == MEDIA_MOVIE_STATE_REC || mode_state == MEDIA_MOVIE_STATE_LAPSE_REC) {
            status = "Recording";
        }
        jl25_legacy_send_property(fd, "Camera.Preview.MJPEG.status.record",
            status, 1);
        return;
    }
    if (jl25_legacy_caseeq(property, "Camera.Preview.RTSP.av")) {
        const char* rtsp_av = "4";

        PARAM_GetMenuParam(&menu);
        if (((menu.ViewWin.Current >> 1) & 0x1) == 1) {
            rtsp_av = "4/5";
        }
        jl25_legacy_send_property(fd, property, rtsp_av, 1);
        return;
    }
    if (strstr(property, "Camera.Preview.") != NULL) {
        const char* rtsp_av = "4";

        PARAM_GetMenuParam(&menu);
        if (((menu.ViewWin.Current >> 1) & 0x1) == 1) {
            rtsp_av = "4/5";
        }
        if (strstr(property, "Source.1.Camid") != NULL) {
            snprintf(s_legacy_camid, sizeof(s_legacy_camid), "%s",
                (s_legacy_camid[0] != '\0') ? s_legacy_camid : "front");
            jl25_legacy_send_property(fd, property, s_legacy_camid, 1);
        } else if (strstr(property, "*") != NULL) {
            char body[256];

            snprintf(body, sizeof(body),
                "0\nOK\n"
                "Camera.Preview.RTSP.av=%s\n"
                "Camera.Preview.Source.1.Camid=%s\n"
                "0\nOK\n",
                rtsp_av, s_legacy_camid);
            jl25_legacy_send_text(fd, body);
        } else {
            jl25_legacy_send_property(fd, "Camera.Preview.RTSP.av",
                rtsp_av, 1);
        }
        return;
    }
    if (strstr(property, "Camera.Menu.") != NULL) {
        if (strstr(property, "menuXML") != NULL) {
            jl25_legacy_send_property(fd, property, "ON", 1);
        } else if (strstr(property, "SDInfo") != NULL) {
            jl25_legacy_send_property(fd, property,
                jl25_legacy_card_available() ? "ON" : "OFF", 1);
        } else if (strstr(property, "FWversion") != NULL) {
            jl25_legacy_send_property(fd, property, JL25_LEGACY_VERSION, 1);
        } else {
            jl25_legacy_send_property(fd, property, "OFF", 1);
        }
        return;
    }
    if (jl25_legacy_caseeq(property, "Video")) {
        jl25_legacy_send_property(fd, property,
            jl25_capture_is_recording() ? "recording" : "idle",
            0);
        return;
    }

    jl25_legacy_send_status(fd, -1);
}

static void jl25_legacy_set_property(int fd, const char* property,
    const char* value)
{
    if (jl25_legacy_caseeq(property, "Heartbeat")) {
        jl25_alive_refresh();
        jl25_legacy_send_status(fd, 0);
        return;
    }

    if (jl25_legacy_is_wifi_setting(property)) {
        /* 热点名称/密码不对 APP 暴露，也不接受修改。 */
        jl25_legacy_send_unsupported(fd);
        return;
    }

    if (jl25_legacy_caseeq(property, "Live") || jl25_legacy_caseeq(property, "StreamStatus")) {
        if (jl25_legacy_caseeq(value, "Enter") || jl25_legacy_caseeq(value, "ON") || jl25_legacy_caseeq(value, "Start") || jl25_legacy_caseeq(value, "1")) {
            if (MODEMNG_GetCurWorkMode() == WORK_MODE_PLAYBACK) {
                jl25_legacy_send_status(fd, 0);
                return;
            }
            if (jl25_preview_start() != 0) {
                MLOG_ERR("jl25 legacy preview start fail, property=[%s] value=[%s]\n",
                    property, value);
            }
        } else if (jl25_legacy_caseeq(value, "Exit") || jl25_legacy_caseeq(value, "OFF") || jl25_legacy_caseeq(value, "Stop") || jl25_legacy_caseeq(value, "0")) {
            if (jl25_preview_stop() != 0) {
                MLOG_ERR("jl25 legacy preview stop fail, property=[%s] value=[%s]\n",
                    property, value);
            }
        }
        jl25_legacy_send_status(fd, 0);
        return;
    }

    if (jl25_legacy_caseeq(property, "Playback")) {
        if (jl25_preview_stop() != 0) {
            MLOG_ERR("jl25 legacy preview stop on playback fail\n");
        }
        jl25_legacy_send_status(fd, 0);
        return;
    }
    if (jl25_legacy_caseeq(property, "Video")) {
        int ret = jl25_legacy_handle_video_action(value);

        if (ret != 0) {
            MLOG_ERR("jl25 legacy video action fail, value=[%s]\n",
                value);
            jl25_legacy_send_status(fd, -1);
            return;
        }
        jl25_legacy_send_status(fd, 0);
        return;
    }

    /* Keep navigation discovery compatible. */
    if (jl25_legacy_caseeq(property, "Setting") || jl25_legacy_caseeq(property, "Playback")) {
        jl25_legacy_send_status(fd, 0);
        return;
    }
    if (jl25_legacy_caseeq(property, "SD0")) {
        int ret = jl25_storage_format_card();
        jl25_legacy_send_status(fd, ret == 0 ? 0 : -1);
        return;
    }
    if (jl25_legacy_caseeq(property, "FactoryReset")) {
        int ret = jl25_factory_reset();
        jl25_legacy_send_status(fd, ret == 0 ? 0 : -1);
        return;
    }

    jl25_legacy_send_status(fd, -1);
}

static void jl25_legacy_cammenu(int fd)
{
    static const char body[] = "<?xml version=\"1.0\"?>\r\n"
                               "<camera xmlns=\"urn:schemas-ait-dvr:device-1-0\">\r\n"
                               "<version>0.1</version>\r\n"
                               "<menu title=\"FORMAT SD\" id=\"SD0\"></menu>\r\n"
                               "<menu title=\"RESET MACHINE\" id=\"FactoryReset\"></menu>\r\n"
                               "</camera>\r\n";

    jl25_http_send_response(fd, 200, "OK", "text/xml; charset=utf-8",
        body, sizeof(body) - 1);
}

static int jl25_legacy_is_thumb_path(const char* path)
{
    return strncmp(path, "/thumb/", 7) == 0;
}

static void jl25_legacy_thumb(int fd, const char* path)
{
    const char* name;

    name = strrchr(path, '/');
    if (name == NULL || name[1] == '\0') {
        jl25_http_send_response(fd, 404, "Not Found", "text/html",
            "{\"Result\":-404}", 15);
        return;
    }
    name++;
    MLOG_INFO("jl25 legacy thumb file=[%s]\n", name);
    jl25_storage_send_thumb_fid(name, fd);
}

void jl25_legacy_handle(const char* path, const char* query, int fd)
{
    char action[32];
    char property[128];
    char value[256];

    jl25_discovery_note_peer(fd);

    if (jl25_legacy_caseeq(path, "/cgi-bin/hisnet/getwifi.cgi") || jl25_legacy_caseeq(path, "/cgi-bin/hisnet/getwifi.cgi/")) {
        /*
		 * This endpoint is used by APP discovery to decide whether the
		 * camera exposes WiFi credential settings.  It is intentionally
		 * unsupported for dc309, just like the actual credential APIs.
		 */
        MLOG_INFO("jl25 legacy getwifi probe rejected with HTTP 404\n");
        jl25_legacy_send_wifi_unsupported(fd);
        return;
    }
    if (jl25_legacy_caseeq(path, "/cammenu.xml")) {
        jl25_legacy_cammenu(fd);
        return;
    }
    if (jl25_legacy_is_thumb_path(path)) {
        jl25_legacy_thumb(fd, path);
        return;
    }
    if (!jl25_legacy_caseeq(path, "/cgi-bin/Config.cgi") && !jl25_legacy_caseeq(path, "/cgi-bin/Config.cgi/")) {
        jl25_legacy_send_status(fd, -1);
        return;
    }

    action[0] = '\0';
    property[0] = '\0';
    value[0] = '\0';
    jl25_legacy_query_get(query, "action", action, sizeof(action));
    jl25_legacy_query_get(query, "property", property, sizeof(property));
    jl25_legacy_query_get(query, "value", value, sizeof(value));
    if (jl25_legacy_is_wifi_setting(property)) {
        MLOG_INFO("jl25 legacy Config action=[%s] property=[%s] value=[redacted]\n",
            action, property);
    } else {
        MLOG_INFO("jl25 legacy Config action=[%s] property=[%s] value=[%s]\n",
            action, property, value);
    }

    if (jl25_legacy_caseeq(action, "get") && property[0] != '\0') {
        jl25_legacy_get_property(fd, property);
        return;
    }
    if ((jl25_legacy_caseeq(action, "set") || jl25_legacy_caseeq(action, "setcamid")) && property[0] != '\0') {
        if (jl25_legacy_caseeq(property, "Camera.Preview.Source.1.Camid") && value[0] != '\0') {
            snprintf(s_legacy_camid, sizeof(s_legacy_camid), "%s", value);
        }
        jl25_legacy_set_property(fd, property, value);
        return;
    }
    if (jl25_legacy_caseeq(action, "dir") && property[0] != '\0') {
        jl25_legacy_get_file_dir(fd, property, query);
        return;
    }
    if (jl25_legacy_caseeq(action, "del")) {
        jl25_legacy_send_status(fd, 0);
        return;
    }
    jl25_legacy_send_status(fd, -1);
}
