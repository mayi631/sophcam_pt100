#include "config.h"
#include "custom.h"
#include "lvgl.h"
#include <stdio.h>

// #############################################################################
// ! #region 公共页面样式定义
// #############################################################################

// cont_top 顶部栏容器样式
lv_style_t style_common_cont_top;

// 页面主容器背景样式（黑色背景）
lv_style_t style_common_main_bg;

// btn_back 返回按钮样式
lv_style_t style_common_btn_back;

// 返回按钮标签样式（白色文字，SourceHanSerifSC_30字体）
lv_style_t style_common_label_back;

// 焦点样式（橙色轮廓）
lv_style_t style_focus_orange;

// 通用 toast 样式
lv_style_t style_toast_popup;

/* 设置项样式 - 深色背景 */
lv_style_t style_settings_item;
/* 设置项选中样式 - 浅色背景 */
lv_style_t style_settings_item_selected;
/* 页面容器样式 - 透明背景、无边框、无内边距 */
lv_style_t style_page_container;
/* 图片按钮样式 - 全透明、无边框 */
lv_style_t style_noboarder;
/* 通用遮罩样式 */
lv_style_t style_overlay_mask;
/* 通用弹层面板样式 */
lv_style_t style_modal_panel;
/* 通用列表偶数行样式 */
lv_style_t style_list_row_even;
/* 通用列表奇数行样式 */
lv_style_t style_list_row_odd;

void init_common_style(void)
{
    // style_common_cont_top
    lv_style_init(&style_common_cont_top);
    lv_style_set_border_width(&style_common_cont_top, 0); // 无边框
    lv_style_set_radius(&style_common_cont_top, 0); // 无圆角
    lv_style_set_bg_opa(&style_common_cont_top, 255); // 不透明背景
    lv_style_set_bg_color(&style_common_cont_top, lv_color_hex(0)); // 黑色背景
    lv_style_set_bg_grad_dir(&style_common_cont_top, LV_GRAD_DIR_NONE); // 无渐变
    lv_style_set_pad_top(&style_common_cont_top, 0); // 无内边距
    lv_style_set_pad_bottom(&style_common_cont_top, 0); // 无内边距
    lv_style_set_pad_left(&style_common_cont_top, 0); // 无内边距
    lv_style_set_pad_right(&style_common_cont_top, 0); // 无内边距
    lv_style_set_shadow_width(&style_common_cont_top, 0); // 无阴影

    // style_common_main_bg
    lv_style_init(&style_common_main_bg);
    lv_style_set_bg_opa(&style_common_main_bg, LV_OPA_COVER); // 不透明背景
    lv_style_set_bg_color(&style_common_main_bg, lv_color_hex(0x000000)); // 黑色背景

    // style_common_btn_back
    lv_style_init(&style_common_btn_back);
    lv_style_set_bg_opa(&style_common_btn_back, 255); // 不透明背景
    lv_style_set_bg_color(&style_common_btn_back, lv_color_hex(0x171717)); // 背景色
    lv_style_set_border_width(&style_common_btn_back, 0); // 无边框
    lv_style_set_radius(&style_common_btn_back, 20); // 圆角20
    lv_style_set_shadow_width(&style_common_btn_back, 0); // 无阴影
    lv_style_set_text_color(&style_common_btn_back, lv_color_hex(0x1A1A1A)); // 文本颜色
    lv_style_set_text_font(&style_common_btn_back, &lv_font_montserratMedium_13); // 字体
    lv_style_set_text_opa(&style_common_btn_back, 255); // 文本不透明
    lv_style_set_text_align(&style_common_btn_back, LV_TEXT_ALIGN_CENTER); // 文本居中
    lv_style_set_pad_top(&style_common_btn_back, 0); // 无内边距
    lv_style_set_pad_bottom(&style_common_btn_back, 0); // 无内边距
    lv_style_set_pad_left(&style_common_btn_back, 0); // 无内边距
    lv_style_set_pad_right(&style_common_btn_back, 0); // 无内边距

    // style_common_label_back
    lv_style_init(&style_common_label_back);
    lv_style_set_text_color(&style_common_label_back, lv_color_hex(0xFFFFFF)); // 白色文字
    lv_style_set_text_font(&style_common_label_back, &lv_font_SourceHanSerifSC_Regular_30); // 字体
    lv_style_set_text_opa(&style_common_label_back, 255); // 文本不透明
    lv_style_set_text_align(&style_common_label_back, LV_TEXT_ALIGN_CENTER); // 文本居中

    // style_focus_orange
    lv_style_init(&style_focus_orange);
    lv_style_set_outline_color(&style_focus_orange, lv_color_hex(0xF09F20)); // 橙色轮廓
    lv_style_set_outline_opa(&style_focus_orange, LV_OPA_COVER); // 轮廓不透明

    // style_toast_popup
    lv_style_init(&style_toast_popup);
    lv_style_set_bg_opa(&style_toast_popup, LV_OPA_60);
    lv_style_set_bg_color(&style_toast_popup, lv_color_black());
    lv_style_set_border_width(&style_toast_popup, 0);
    lv_style_set_radius(&style_toast_popup, 22);
    lv_style_set_pad_hor(&style_toast_popup, 20);
    lv_style_set_pad_ver(&style_toast_popup, 10);

    /* 初始化设置项样式 */
    lv_style_init(&style_settings_item);
    lv_style_set_bg_opa(&style_settings_item, LV_OPA_COVER);
    lv_style_set_bg_color(&style_settings_item, lv_color_hex(0x1A1A1A)); /* 深色背景 */
    lv_style_set_border_width(&style_settings_item, 0);
    lv_style_set_pad_top(&style_settings_item, 12);
    lv_style_set_pad_bottom(&style_settings_item, 12);
    lv_style_set_pad_left(&style_settings_item, 15);
    lv_style_set_pad_right(&style_settings_item, 15);

    /* 初始化设置项选中样式 */
    lv_style_init(&style_settings_item_selected);
    lv_style_set_bg_opa(&style_settings_item_selected, LV_OPA_COVER);
    lv_style_set_bg_color(&style_settings_item_selected, lv_color_hex(0x2A2A2A)); /* 浅色背景表示选中 */
    lv_style_set_border_width(&style_settings_item_selected, 0);
    lv_style_set_pad_top(&style_settings_item_selected, 12);
    lv_style_set_pad_bottom(&style_settings_item_selected, 12);
    lv_style_set_pad_left(&style_settings_item_selected, 15);
    lv_style_set_pad_right(&style_settings_item_selected, 15);

    /* 初始化页面容器样式 */
    lv_style_init(&style_page_container); /* 创建样式对象 */
    lv_style_set_border_width(&style_page_container, 0); /* 无边框 */
    lv_style_set_radius(&style_page_container, 0); /* 无圆角 */
    lv_style_set_bg_opa(&style_page_container, LV_OPA_TRANSP); /* 背景透明 */
    lv_style_set_pad_top(&style_page_container, 0); /* 无内边距 */
    lv_style_set_pad_bottom(&style_page_container, 0);
    lv_style_set_pad_left(&style_page_container, 0);
    lv_style_set_pad_right(&style_page_container, 0);
    lv_style_set_pad_row(&style_page_container, 0); /* FLEX 布局无行间距 */
    lv_style_set_margin_top(&style_page_container, 0); /* 无外边距 */
    lv_style_set_margin_bottom(&style_page_container, 0);
    lv_style_set_margin_left(&style_page_container, 0);
    lv_style_set_margin_right(&style_page_container, 0);

    /* 初始化无边框-全透明样式 */
    lv_style_init(&style_noboarder); /* 创建样式对象 */
    lv_style_set_bg_opa(&style_noboarder, LV_OPA_TRANSP); /* 背景完全透明 */
    lv_style_set_border_width(&style_noboarder, 0); /* 无边框 */
    lv_style_set_radius(&style_noboarder, 0); /* 无圆角 */
    lv_style_set_shadow_width(&style_noboarder, 0); /* 无阴影 */
    lv_style_set_pad_top(&style_noboarder, 0); /* 无内边距 */
    lv_style_set_pad_bottom(&style_noboarder, 0);
    lv_style_set_pad_left(&style_noboarder, 0);
    lv_style_set_pad_right(&style_noboarder, 0);

    /* 初始化通用遮罩样式 */
    lv_style_init(&style_overlay_mask);
    lv_style_set_bg_opa(&style_overlay_mask, LV_OPA_60);
    lv_style_set_bg_color(&style_overlay_mask, lv_color_black());
    lv_style_set_border_width(&style_overlay_mask, 0);
    lv_style_set_radius(&style_overlay_mask, 0);

    /* 初始化通用弹层面板样式 */
    lv_style_init(&style_modal_panel);
    lv_style_set_bg_opa(&style_modal_panel, LV_OPA_COVER);
    lv_style_set_bg_color(&style_modal_panel, lv_color_hex(0x1E1E1E));
    lv_style_set_border_width(&style_modal_panel, 0);
    lv_style_set_radius(&style_modal_panel, 12);

    /* 初始化通用列表行样式（偶数） */
    lv_style_init(&style_list_row_even);
    lv_style_set_radius(&style_list_row_even, 0);
    lv_style_set_bg_opa(&style_list_row_even, LV_OPA_COVER);
    lv_style_set_bg_color(&style_list_row_even, lv_color_hex(0x151515));
    lv_style_set_border_side(&style_list_row_even, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&style_list_row_even, 1);
    lv_style_set_border_color(&style_list_row_even, lv_color_hex(0x2E2E2E));

    /* 初始化通用列表行样式（奇数） */
    lv_style_init(&style_list_row_odd);
    lv_style_set_radius(&style_list_row_odd, 0);
    lv_style_set_bg_opa(&style_list_row_odd, LV_OPA_COVER);
    lv_style_set_bg_color(&style_list_row_odd, lv_color_hex(0x191919));
    lv_style_set_border_side(&style_list_row_odd, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_width(&style_list_row_odd, 1);
    lv_style_set_border_color(&style_list_row_odd, lv_color_hex(0x2E2E2E));
}

// #endregion
