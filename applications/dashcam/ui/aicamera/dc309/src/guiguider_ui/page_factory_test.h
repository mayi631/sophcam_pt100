#ifndef __PAGE_FACTORY_TEST_H_
#define __PAGE_FACTORY_TEST_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

extern lv_obj_t* obj_factory_test_s; // 工厂测试主窗口
extern lv_obj_t* obj_touch_test_s; // 触摸划线测试窗口
extern lv_obj_t* obj_key_test_s; // 按键测试窗口

/**
 * @brief 工厂测试菜单页面
 */
void factory_test_menu(lv_ui_t* ui);

/**
 * @brief 触摸划线测试页面
 */
void factory_touch_test(lv_ui_t* ui);

/**
 * @brief 按键测试页面
 */
void factory_key_test(lv_ui_t* ui);

/**
 * @brief 闪光灯测试页面
 */
void factory_flash_test(lv_ui_t* ui);

#ifdef __cplusplus
}
#endif
#endif /* __PAGE_FACTORY_TEST_H_ */
