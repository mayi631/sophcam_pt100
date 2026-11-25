#ifndef __STYLE_COMMON_H__
#define __STYLE_COMMON_H__

#include "lvgl.h"

extern lv_style_t style_common_cont_top;
extern lv_style_t style_common_main_bg;
extern lv_style_t style_common_btn_back;
extern lv_style_t style_common_label_back;
extern lv_style_t style_focus_orange;
extern lv_style_t style_toast_popup;
/* 设置项样式 - 深色背景 */
extern lv_style_t style_settings_item;
/* 设置项选中样式 - 浅色背景 */
extern lv_style_t style_settings_item_selected;
/* 页面容器样式 - 透明背景、无边框、无内边距 */
extern lv_style_t style_page_container;
/* 图片按钮样式 - 全透明、无边框 */
extern lv_style_t style_noboarder;
/* 通用遮罩样式 */
extern lv_style_t style_overlay_mask;
/* 通用弹层面板样式 */
extern lv_style_t style_modal_panel;

/* 通用列表偶数行样式 */
extern lv_style_t style_list_row_even;
/* 通用列表奇数行样式 */
extern lv_style_t style_list_row_odd;

void init_common_style(void);
void ui_settings_title_create(lv_obj_t *parent, const char *text);

#endif
