
#ifndef __VOICE_BAR_H
#define __VOICE_BAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"
#include <stdbool.h>

lv_obj_t* get_arc_handel(void);
void voice_setting_arc_create(void);
void voice_arc_delete(void);

void volume_add(void);
void volume_reduce(void);

int32_t do_zoomin(int32_t key_value);
int32_t do_zoomout(int32_t key_value);

const char* volume_icon_get(void);

// 回放音量控制函数
int32_t get_album_volume_level(void);
void set_album_volume_level(int32_t level);
int32_t get_sysmenu_volume_level(void);
void set_sysmenu_volume_level(int32_t level);
void enter_album_volume_mode(void);
void exit_album_volume_mode(void);
bool is_in_album_mode(void);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
