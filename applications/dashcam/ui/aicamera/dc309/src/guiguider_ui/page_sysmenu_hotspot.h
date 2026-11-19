/*
 * 手机APP连接（相机热点）页面
 * 功能：手机连接相机热点，通过APP下发协议命令进行下载图片、拍照录像、参数设置等操作。
 * 当前为 UI 骨架页面，具体协议命令后续实现。
 */
#ifndef __PAGE_SYSMENU_HOTSPOT_H_
#define __PAGE_SYSMENU_HOTSPOT_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

extern lv_obj_t* obj_sysMenu_Hotspot_s; // 底层窗口
void sysMenu_Hotspot(lv_ui_t* ui);

#ifdef __cplusplus
}
#endif
#endif /* __PAGE_SYSMENU_HOTSPOT_H_ */
