
#ifndef __PAGE_SETTING_SHOOTMODE_H_
#define __PAGE_SETTING_SHOOTMODE_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

typedef enum {
    PHOTO_SHOOTMODE_1 = 0,
    PHOTO_SHOOTMODE_3,
    PHOTO_SHOOTMODE_10,
    PHOTO_SHOOTMODE_MAX,
} photo_shootmode_e;

uint8_t get_shootmode(bool mode);
void set_shootmode(uint8_t index);
void photoMenu_ShootingMode(lv_ui_t *ui);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
