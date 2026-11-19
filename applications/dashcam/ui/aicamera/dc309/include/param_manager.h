#ifndef __PARAM_MANAGER_H__
#define __PARAM_MANAGER_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PARAM_ID_WIFI_CONNECTED = 0,
    PARAM_ID_WIFI_SIGNAL_DBM,
    PARAM_ID_WIFI_ENABLED,
    PARAM_ID_WIFI_BUTT
} param_id_t;

int param_manager_init(void);
void param_manager_deinit(void);
void param_manager_poll(void);
int param_manager_get(param_id_t id);
int param_manager_set(param_id_t id, int value);
int param_manager_set_wifi_ssid(const char* ssid);
int param_manager_get_wifi_ssid(char* out_ssid, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* __PARAM_MANAGER_H__ */
