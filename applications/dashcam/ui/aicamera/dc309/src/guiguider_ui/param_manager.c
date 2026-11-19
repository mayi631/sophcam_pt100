#include "param_manager.h"
#include <pthread.h>
#include <string.h>

#define PARAM_WIFI_SSID_MAX_LEN 128

static pthread_mutex_t g_param_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_initialized = 0;
static int g_values[PARAM_ID_WIFI_BUTT];
static char g_wifi_ssid[PARAM_WIFI_SSID_MAX_LEN];

static int is_valid_param_id(param_id_t id)
{
    return id >= PARAM_ID_WIFI_CONNECTED && id < PARAM_ID_WIFI_BUTT;
}

int param_manager_init(void)
{
    pthread_mutex_lock(&g_param_mutex);
    if (g_initialized) {
        pthread_mutex_unlock(&g_param_mutex);
        return 0;
    }

    memset(g_values, 0, sizeof(g_values));
    memset(g_wifi_ssid, 0, sizeof(g_wifi_ssid));
    g_values[PARAM_ID_WIFI_SIGNAL_DBM] = -1;
    g_initialized = 1;
    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

void param_manager_deinit(void)
{
    pthread_mutex_lock(&g_param_mutex);
    g_initialized = 0;
    memset(g_values, 0, sizeof(g_values));
    g_values[PARAM_ID_WIFI_SIGNAL_DBM] = -1;
    memset(g_wifi_ssid, 0, sizeof(g_wifi_ssid));
    pthread_mutex_unlock(&g_param_mutex);
}

void param_manager_poll(void)
{
    pthread_mutex_lock(&g_param_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_param_mutex);
        return;
    }
    pthread_mutex_unlock(&g_param_mutex);
}

int param_manager_get(param_id_t id)
{
    int value = -1;

    pthread_mutex_lock(&g_param_mutex);
    if (!g_initialized || !is_valid_param_id(id)) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    value = g_values[id];
    pthread_mutex_unlock(&g_param_mutex);
    return value;
}

int param_manager_set(param_id_t id, int value)
{
    pthread_mutex_lock(&g_param_mutex);
    if (!g_initialized || !is_valid_param_id(id)) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    g_values[id] = value;
    if (id == PARAM_ID_WIFI_CONNECTED && value == 0) {
        g_values[PARAM_ID_WIFI_SIGNAL_DBM] = -1;
        g_wifi_ssid[0] = '\0';
    } else if (id == PARAM_ID_WIFI_ENABLED && value == 0) {
        g_values[PARAM_ID_WIFI_CONNECTED] = 0;
        g_values[PARAM_ID_WIFI_SIGNAL_DBM] = -1;
        g_wifi_ssid[0] = '\0';
    }
    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int param_manager_set_wifi_ssid(const char* ssid)
{
    pthread_mutex_lock(&g_param_mutex);
    if (!g_initialized) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    if (ssid == NULL) {
        g_wifi_ssid[0] = '\0';
    } else {
        strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid) - 1);
        g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
    }
    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}

int param_manager_get_wifi_ssid(char* out_ssid, size_t out_size)
{
    pthread_mutex_lock(&g_param_mutex);
    if (!g_initialized || out_ssid == NULL || out_size == 0) {
        pthread_mutex_unlock(&g_param_mutex);
        return -1;
    }

    strncpy(out_ssid, g_wifi_ssid, out_size - 1);
    out_ssid[out_size - 1] = '\0';
    pthread_mutex_unlock(&g_param_mutex);
    return 0;
}
