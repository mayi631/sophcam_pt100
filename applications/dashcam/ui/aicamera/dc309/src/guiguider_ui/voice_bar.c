#define DEBUG
#include "ui_layout.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui_guider.h"
#include "page_all.h"
#include "custom.h"
#include "config.h"
#include "mlog.h"
#include "ui_common.h"

#include "image_recognize/image_recognize.h"
#include "common/extract_thumbnail.h"
#include "common/takephoto.h"
#include "indev.h"
#include <linux/input.h>
#include "mapi_ao.h"
//音量设置变量
static lv_obj_t *value_label = NULL;  // 全局变量用于存储值标签
static lv_obj_t *voice_set_bar = NULL;
// 长按zoomin按键检测相关变量
static lv_timer_t *zoom_in_long_press_timer = NULL;
static bool zoomin_long_press_flag         = false;
// 长按zoomout按键检测相关变量
static lv_timer_t* zoom_out_long_press_timer = NULL;
static bool zoomout_long_press_flag = false;
// 音量预览相关变量 - 用于滑动过程中暂存音量值,松手后才实际设置
static int32_t pending_volume_value = -1; // -1表示有待处理的音量值
static bool is_dragging = false; // 是否正在拖拽

// 回放音量变量 - 用于视频回放等场景，与系统音量分开
static int32_t album_volume_level = 0; // 回放音量
// 系统音量备份 - 进入回放时备份系统音量，退出时恢复
static int32_t sysmenu_volume_level = 0; // 系统音量备份
static bool is_album_mode = false; // 是否处于回放模式

#define MAX_VOLUME 32
#define HALF_MAX_VOLUME (MAX_VOLUME / 2)

/* 获取回放音量 */
int32_t get_album_volume_level(void)
{
    return album_volume_level;
}

/* 设置回放音量 */
void set_album_volume_level(int32_t level)
{
    if (level < 0)
        level = 0;
    if (level > HALF_MAX_VOLUME)
        level = HALF_MAX_VOLUME;
    album_volume_level = level;
    MLOG_DBG("设置回放音量: %d\n", album_volume_level);
}

/* 获取系统音量备份 */
int32_t get_sysmenu_volume_level(void)
{
    return sysmenu_volume_level;
}

/* 设置系统音量备份 */
void set_sysmenu_volume_level(int32_t level)
{
    sysmenu_volume_level = level;
}

/* 进入回放模式：备份当前系统音量，切换到回放音量 */
void enter_album_volume_mode(void)
{
    // 备份当前系统音量
    MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &sysmenu_volume_level);
    is_album_mode = true;
    MLOG_DBG("进入回放模式，备份系统音量: %d\n", sysmenu_volume_level);

    // 如果回放音量为0，则使用系统音量作为初始值
    if (album_volume_level == 0) {
        album_volume_level = sysmenu_volume_level;
    }

    // 应用回放音量
    MAPI_AO_SetVolume(MEDIA_GetCtx()->SysHandle.aohdl, album_volume_level);
    MLOG_DBG("应用回放音量: %d\n", album_volume_level);
}

/* 退出回放模式：恢复系统音量 */
void exit_album_volume_mode(void)
{
    if (is_album_mode) {
        // 恢复系统音量
        MAPI_AO_SetVolume(MEDIA_GetCtx()->SysHandle.aohdl, sysmenu_volume_level);
        MLOG_DBG("退出回放模式，恢复系统音量: %d\n", sysmenu_volume_level);
        is_album_mode = false;
    }
}

/* 检查是否处于回放模式 */
bool is_in_album_mode(void)
{
    return is_album_mode;
}

const char* volume_icon_get(void)
{
    int volume_level = 0;
    // 如果在回放模式，使用回放音量；否则使用系统音量
    if (is_album_mode) {
        volume_level = album_volume_level;
    } else {
        MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &volume_level);
    }
    int percentage = (volume_level * 100) / HALF_MAX_VOLUME;

    static char* target_icon = LV_SYMBOL_VOLUME_MID;
    if (percentage == 0 && percentage <= 5) {
        target_icon = LV_SYMBOL_MUTE; // 0-30%: 静音
    } else if (percentage > 5 && percentage <= 50) {
        target_icon = LV_SYMBOL_VOLUME_MID; // 31-70%: 中等音量
    } else if (percentage > 70 && percentage <= 100) {
        target_icon = LV_SYMBOL_VOLUME_MAX; // 71-100%: 最大音量
    } else {
        MLOG_ERR("percentage %d not in range [0, 100], icon may error\n", percentage);
    }

    return target_icon;
}

// 递归查找并替换音量图标的辅助函数
static void find_and_replace_volume_icon(lv_obj_t* parent, uint8_t current_value)
{
    if (!parent || !lv_obj_is_valid(parent)) {
        return;
    }

    int percentage = (current_value * 100) / HALF_MAX_VOLUME;
    // 根据音量确定目标图标
    const char* target_icon = volume_icon_get();

    if (!target_icon) {
        return; // 无效范围
    }

    // 遍历所有子对象
    uint32_t child_count = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(parent, i);
        if (!child || !lv_obj_is_valid(child)) {
            continue;
        }

        // 检查对象类型是否为label
        if (lv_obj_check_type(child, &lv_label_class)) {
            // 获取label文本
            const char* text = lv_label_get_text(child);
            if (text) {
                // 检查是否是音量相关的图标
                if (strcmp(text, LV_SYMBOL_MUTE) == 0 || strcmp(text, LV_SYMBOL_VOLUME_MID) == 0 || strcmp(text, LV_SYMBOL_VOLUME_MAX) == 0) {
                    // 只有当图标需要改变时才更新
                    if (strcmp(text, target_icon) != 0) {
                        lv_label_set_text(child, target_icon);
                        MLOG_DBG("音量 %d%% (%d/%d), 设置图标: %s -> %s\n",
                            percentage, current_value, HALF_MAX_VOLUME, text, target_icon);
                    }
                    break; // 找到一个音量图标后就退出，避免重复修改
                }
            }
        }
        // 递归检查子对象的子对象
        if (lv_obj_get_child_cnt(child) > 0) {
            find_and_replace_volume_icon(child, current_value);
        }
    }
}

// 值改变事件回调函数
static void value_changed_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_target(e);
    if(value_label && lv_obj_is_valid(value_label)) {
        lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(bar) * 100) / HALF_MAX_VOLUME);
        // 获取当前屏幕作为搜索的根对象
        lv_obj_t *current_screen = lv_scr_act();
        // 递归查找并替换音量图标
        if (current_screen && lv_obj_is_valid(current_screen)&&lv_obj_is_valid(bar)) {
            find_and_replace_volume_icon(current_screen,lv_bar_get_value(bar));
        }
    }
}

/* 应用音量设置 - 用于滑动松手后实际设置音量 */
static void apply_volume_value(int32_t value)
{
    PARAM_MEDIA_COMM_S media_comm = { 0 };

    // 如果在回放模式，更新回放音量；否则更新系统音量
    if (is_album_mode) {
        album_volume_level = value;
        MLOG_DBG("更新回放音量: %d\n", value);
    } else {
        // 保存系统音量配置
        PARAM_GetMediaComm(&media_comm);
        media_comm.Ao.volume = value;
        PARAM_SetMediaComm(&media_comm);
        MLOG_DBG("更新系统音量: %d\n", value);
    }

    // 设置音量
    MAPI_AO_SetVolume(MEDIA_GetCtx()->SysHandle.aohdl, value);

    MLOG_DBG("音量设置为: %d (%d%%)\n", value, (value * 100) / HALF_MAX_VOLUME);
}

void volume_add(void)
{
    int32_t volume_level = 0;
    // 如果在回放模式，使用回放音量；否则使用系统音量
    if (is_album_mode) {
        volume_level = album_volume_level;
    } else {
        MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &volume_level);
    }
    volume_level++;
    if (volume_level > HALF_MAX_VOLUME) {
        volume_level = HALF_MAX_VOLUME;
    }

    // 更新UI显示
    lv_bar_set_value(voice_set_bar, volume_level, LV_ANIM_ON);
    lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(voice_set_bar) * 100) / HALF_MAX_VOLUME);
    lv_obj_send_event(voice_set_bar, LV_EVENT_VALUE_CHANGED, NULL);

    // 立即应用音量设置
    apply_volume_value(volume_level);
}

void volume_reduce(void)
{
    int32_t volume_level = 0;
    // 如果在回放模式，使用回放音量；否则使用系统音量
    if (is_album_mode) {
        volume_level = album_volume_level;
    } else {
        MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &volume_level);
    }
    volume_level--;
    if (volume_level < 0) {
        volume_level = 0;
    }

    // 更新UI显示
    lv_bar_set_value(voice_set_bar, volume_level, LV_ANIM_ON);
    lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(voice_set_bar) * 100) / HALF_MAX_VOLUME);
    lv_obj_send_event(voice_set_bar, LV_EVENT_VALUE_CHANGED, NULL);

    // 立即应用音量设置
    apply_volume_value(volume_level);
}

static void custom_bar_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* bar = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        // 按下事件,开始拖拽
        is_dragging = true;
        pending_volume_value = -1; // 重置待处理值

        // 获取点击位置
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);

        // 转换为相对于进度条的位置
        lv_area_t bar_coords;
        lv_obj_get_coords(bar, &bar_coords);
        lv_coord_t click_y = p.y - bar_coords.y1;

        // 计算进度值(0-HALF_MAX_VOLUME)
        lv_coord_t bar_height = lv_obj_get_height(bar);
        int value = HALF_MAX_VOLUME - (click_y * HALF_MAX_VOLUME / bar_height);

        // 限制在0-HALF_MAX_VOLUME
        value = value < 0 ? 0 : (value > HALF_MAX_VOLUME ? HALF_MAX_VOLUME : value);

        // 只更新UI显示,不设置音量
        lv_bar_set_value(bar, value, LV_ANIM_OFF); // 按下时无动画,更跟手
        lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(bar) * 100) / HALF_MAX_VOLUME);
        lv_obj_send_event(voice_set_bar, LV_EVENT_VALUE_CHANGED, NULL);

    } else if (code == LV_EVENT_PRESSING) {
        // 持续按压事件,处理滑动
        if (!is_dragging)
            return;

        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);

        // 转换为相对于进度条的位置
        lv_area_t bar_coords;
        lv_obj_get_coords(bar, &bar_coords);
        lv_coord_t click_y = p.y - bar_coords.y1;

        // 计算进度值
        lv_coord_t bar_height = lv_obj_get_height(bar);
        int value = HALF_MAX_VOLUME - (click_y * HALF_MAX_VOLUME / bar_height);

        // 限制在范围内
        value = value < 0 ? 0 : (value > HALF_MAX_VOLUME ? HALF_MAX_VOLUME : value);

        // 只更新UI显示,不设置音量
        lv_bar_set_value(bar, value, LV_ANIM_OFF); // 滑动时无动画,更跟手
        lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(bar) * 100) / HALF_MAX_VOLUME);
        lv_obj_send_event(voice_set_bar, LV_EVENT_VALUE_CHANGED, NULL);

        // 保存待设置的音量值,松手时才应用
        pending_volume_value = value;

    } else if (code == LV_EVENT_RELEASED) {
        // 释放事件,应用音量设置
        is_dragging = false;

        if (pending_volume_value >= 0) {
            // 应用音量设置
            apply_volume_value(pending_volume_value);
            pending_volume_value = -1; // 重置待处理值
        }
    } else if (code == LV_EVENT_CLICKED) {
        // 点击事件,快速跳转音量
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);

        // 转换为相对于进度条的位置
        lv_area_t bar_coords;
        lv_obj_get_coords(bar, &bar_coords);
        lv_coord_t click_y = p.y - bar_coords.y1;

        // 计算进度值
        lv_coord_t bar_height = lv_obj_get_height(bar);
        int value = HALF_MAX_VOLUME - (click_y * HALF_MAX_VOLUME / bar_height);

        // 限制在范围内
        value = value < 0 ? 0 : (value > HALF_MAX_VOLUME ? HALF_MAX_VOLUME : value);

        // 更新UI显示
        lv_bar_set_value(bar, value, LV_ANIM_ON); // 点击时使用动画
        lv_label_set_text_fmt(value_label, "%d%%", (lv_bar_get_value(bar) * 100) / HALF_MAX_VOLUME);
        lv_obj_send_event(voice_set_bar, LV_EVENT_VALUE_CHANGED, NULL);

        // 立即应用音量设置
        apply_volume_value(value);
    }
}

void voice_setting_arc_create(void)
{
    int32_t volume_level = 0;
    // 如果在回放模式，使用回放音量；否则使用系统音量
    if (is_album_mode) {
        volume_level = album_volume_level;
        MLOG_DBG("回放模式，使用回放音量: %d\n", volume_level);
    } else {
        MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &volume_level);
        MLOG_DBG("系统模式，使用系统音量: %d\n", volume_level);
    }
    // 创建自定义的垂直进度条
    voice_set_bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(voice_set_bar, UI_X(50), UI_Y(250));
    lv_obj_align(voice_set_bar, LV_ALIGN_RIGHT_MID, UI_X(-20), 0);
    lv_bar_set_range(voice_set_bar, 0, HALF_MAX_VOLUME);
    lv_bar_set_value(voice_set_bar, volume_level, LV_ANIM_OFF); // 设置初始值

    // 设置进度条样式
    lv_obj_set_style_bg_color(voice_set_bar, lv_color_hex(0x4a4a4a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(voice_set_bar, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(voice_set_bar, 15, LV_PART_MAIN);

    // 设置进度条指示器样式
    lv_obj_set_style_bg_color(voice_set_bar, lv_color_hex(0x4da6ff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(voice_set_bar, LV_OPA_30, LV_PART_INDICATOR);
    lv_obj_set_style_radius(voice_set_bar, 15, LV_PART_INDICATOR);

    // 设置为可点击和可拖动
    lv_obj_add_flag(voice_set_bar, LV_OBJ_FLAG_CLICKABLE);

    // 注册事件处理函数
    lv_obj_add_event_cb(voice_set_bar, custom_bar_event_cb, LV_EVENT_ALL, NULL);

    // 添加一个标签显示当前值
    value_label = lv_label_create(lv_scr_act());
    lv_obj_align_to(value_label, voice_set_bar, LV_ALIGN_OUT_BOTTOM_MID, UI_X(4), UI_Y(10));
    lv_label_set_text_fmt(value_label, "%d%%",(lv_bar_get_value(voice_set_bar) * 100) / HALF_MAX_VOLUME );
    lv_obj_set_style_text_color(value_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(value_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 20), 0);

    // 添加值改变事件回调
    lv_obj_add_event_cb(voice_set_bar, value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *get_arc_handel(void)
{
    return voice_set_bar;
}

void voice_arc_delete(void)
{
    // 先删除长按定时器，防止定时器回调访问已删除的对象
    if (zoom_in_long_press_timer != NULL) {
        lv_timer_del(zoom_in_long_press_timer);
        zoom_in_long_press_timer = NULL;
    }
    if (zoom_out_long_press_timer != NULL) {
        lv_timer_del(zoom_out_long_press_timer);
        zoom_out_long_press_timer = NULL;
    }
    zoomin_long_press_flag = false;
    zoomout_long_press_flag = false;

    if (voice_set_bar != NULL) {
        // 使用 safe_delete_obj 安全删除
        if (lv_obj_is_valid(voice_set_bar)) {
            lv_obj_del(voice_set_bar);
        }
        voice_set_bar = NULL;
    }
    // value_label 需要单独检查，因为它可能和 voice_set_bar 不在同一个父对象上
    if (value_label != NULL) {
        if (lv_obj_is_valid(value_label)) {
            lv_obj_del(value_label);
        }
        value_label = NULL;
    }
    // 重置拖拽状态
    is_dragging = false;
    pending_volume_value = -1;
}

// 长zoomin按键检测定时器回调函数
static void zoomin_long_press_timer_cb(lv_timer_t *t)
{
    // 定时器触发说明按键已经持续按下300ms，执行长按逻辑
    zoomin_long_press_flag = true;
    // 100ms周期继续检测, 直到按键松开
    lv_timer_set_period(t, 100);
    lv_timer_reset(t);

    voice_arc_delete();
    voice_setting_arc_create();
    volume_add();

    if(!lv_obj_is_valid(voice_set_bar)) {
        if(zoom_in_long_press_timer != NULL) {
            lv_timer_del(zoom_in_long_press_timer);
            zoom_in_long_press_timer = NULL;
            zoomin_long_press_flag = false;
        }
    }
}

int32_t do_zoomin(int32_t key_value)
{
    if(key_value == 1) {
        // 创建长按检测定时器, 300ms认为是长按
        zoomin_long_press_flag  = false;
        zoom_in_long_press_timer = lv_timer_create(zoomin_long_press_timer_cb, 300, NULL);
    } else {
        // zoomin(T)按键释放
        // 短按或者长按，在按键释放的时候都要删除定时器
        if(zoom_in_long_press_timer != NULL) {
            lv_timer_del(zoom_in_long_press_timer);
            zoom_in_long_press_timer = NULL;
        }
        // 短按触发一次放大
        if(!zoomin_long_press_flag) {
            MLOG_DBG("zoomin按键短按, 执行短按逻辑\n");
            voice_arc_delete();
            voice_setting_arc_create();
            volume_add();
        }
    }
    return 0;
}

// 长zoomout按键检测定时器回调函数
static void zoomout_long_press_timer_cb(lv_timer_t *t)
{
    // 定时器触发说明按键已经持续按下300ms，执行长按逻辑
    zoomout_long_press_flag = true;
    // 100ms周期继续检测, 直到按键松开
    lv_timer_set_period(t, 100);
    lv_timer_reset(t);

    voice_arc_delete();
    voice_setting_arc_create();
    volume_reduce();

    if(!lv_obj_is_valid(voice_set_bar)) {
        if(zoom_out_long_press_timer != NULL) {
            lv_timer_del(zoom_out_long_press_timer);
            zoom_out_long_press_timer = NULL;
            zoomout_long_press_flag = false;
        }
    }
}

int32_t do_zoomout(int32_t key_value)
{
    if(key_value == 1) {
        // 创建长按检测定时器, 300ms认为是长按
        zoomout_long_press_flag  = false;
        zoom_out_long_press_timer = lv_timer_create(zoomout_long_press_timer_cb, 300, NULL);
    } else {
        // zoomin(T)按键释放
        // 短按或者长按，在按键释放的时候都要删除定时器
        if(zoom_out_long_press_timer != NULL) {
            lv_timer_del(zoom_out_long_press_timer);
            zoom_out_long_press_timer = NULL;
        }
        // 短按触发一次缩小
        if(!zoomout_long_press_flag) {
            MLOG_DBG("zoomout按键短按, 执行短按逻辑\n");
            voice_arc_delete();
            voice_setting_arc_create();
            volume_reduce();
        }
    }
    return 0;
}
