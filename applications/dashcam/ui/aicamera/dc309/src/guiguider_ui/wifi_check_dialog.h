
#ifndef __WIFI_CHECK_DIALOG_H_
#define __WIFI_CHECK_DIALOG_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "gui_guider.h"

typedef void (*wifi_return_cb_t)(void* user_data);

/**
 * @brief WiFi跳转前资源释放回调类型
 */
typedef void (*wifi_release_cb_t)(void);

/**
 * @brief 检查WiFi连接状态，如果未连接则显示提示对话框
 *
 * @param parent_screen 父窗口对象，对话框将创建在此窗口上
 * @param return_cb 返回时的回调函数
 * @param user_data 传递给return_cb的用户数据
 * @param release_cb 跳转到WiFi列表前执行的资源释放回调（可为NULL）
 * @return true WiFi已连接，无需显示对话框
 * @return false WiFi未连接，已显示对话框
 */
bool wifi_check_and_show_dialog(lv_obj_t* parent_screen,
    wifi_return_cb_t return_cb, wifi_release_cb_t release_cb,
    void* user_data);

/**
 * @brief 关闭WiFi检查对话框
 */
int wifi_check_dialog_close(void);

void wifi_check_set_return_handler(wifi_return_cb_t cb, void *user_data);
wifi_return_cb_t wifi_check_get_return_handler(void);
void *wifi_check_get_return_userdata(void);
int wifi_check_get_return_flag(void);
void wifi_check_clear_return_handler(void);

/**
 * @brief 检查WiFi是否已连接
 *
 * @return true WiFi已连接
 * @return false WiFi未连接
 */
bool wifi_check_is_connected(void);

#ifdef __cplusplus
}
#endif
#endif /* __WIFI_CHECK_DIALOG_H_ */

