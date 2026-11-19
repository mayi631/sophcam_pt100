
#ifndef __WIFI_IMG_H
#define __WIFI_IMG_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>

// 获取wifi信号强度
int wifi_level_get(void);
int wifi_level_from_dbm(int dbm);
void delete_wifi_img(void);
void create_wifi_img(lv_obj_t* parent, int32_t x_ofs, int32_t y_ofs);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
