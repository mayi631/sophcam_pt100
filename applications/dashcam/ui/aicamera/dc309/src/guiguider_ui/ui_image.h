#ifndef UI_IMAGE_H
#define UI_IMAGE_H

#include "lvgl.h"

static inline lv_obj_t *ui_image_create(lv_obj_t *parent)
{
    lv_obj_t *image = lv_image_create(parent);
    if (image != NULL) {
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
    }
    return image;
}

static inline lv_obj_t *ui_imagebutton_create(lv_obj_t *parent)
{
    lv_obj_t *image = ui_image_create(parent);
    if (image != NULL) {
        lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_ADV_HITTEST);
    }
    return image;
}

#endif
