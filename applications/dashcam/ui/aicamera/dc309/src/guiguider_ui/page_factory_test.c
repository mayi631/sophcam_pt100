// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#define DEBUG
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "page_all.h"
#include "param_manager.h"
#include "style_common.h"
#include "ui_common.h"
#include "wifi_manager.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define GRID_COLS 1
#define GRID_ROWS 4
#define GRID_MAX_OBJECTS GRID_ROWS* GRID_COLS

// 闪光灯GPIO定义 (需要根据实际硬件修改)
#define FLASH_LED_GPIO_NUM 352
#define FLASH_LED_PULSE 13

// 工厂测试菜单按钮定义
#define FACTORY_TEST_ITEMS_COUNT 4
typedef struct {
    const char* name;
    void (*event_handler)(lv_event_t* e);
} factory_test_item_t;

// #endregion
// #############################################################################
// ! #region 2. 数据结构定义
// #############################################################################

// 划线测试路径点
typedef struct {
    int16_t x;
    int16_t y;
} point_t;

// #endregion
// #############################################################################
// ! #region 3. 全局变量 & 函数声明
// #############################################################################

lv_obj_t* obj_factory_test_s = NULL; // 工厂测试主窗口
lv_obj_t* obj_touch_test_s = NULL; // 触摸划线测试窗口
lv_obj_t* obj_key_test_s = NULL; // 按键测试窗口

// 触摸测试相关
static lv_obj_t* touch_canvas = NULL;
static lv_obj_t* touch_overlay = NULL;
static lv_obj_t* touch_test_label = NULL;
static lv_obj_t* touch_test_progress = NULL;

// 9点连接测试相关
#define TOUCH_TEST_POINT_COUNT 9
#define TOUCH_TEST_TOLERANCE 50 // 点检测容差
static lv_point_t touch_test_points[TOUCH_TEST_POINT_COUNT]; // 9个测试点坐标
static bool touch_point_tested[TOUCH_TEST_POINT_COUNT]; // 点是否已连接
static uint8_t touch_skipped_count = 0; // 被跳过的点数
static lv_obj_t* touch_point_labels[TOUCH_TEST_POINT_COUNT]; // 点编号标签
static lv_point_t touch_last_touch_pos; // 上次触摸位置
static bool is_drawing = false; // 是否正在进行划线操作

// 闪光灯测试相关
static lv_obj_t* obj_flash_test_s = NULL; // 闪光灯测试窗口
static lv_obj_t* flash_test_label = NULL; // 闪光灯状态标签
static bool flash_is_on = false; // 闪光灯当前状态

// 工厂WiFi测试相关
static lv_obj_t* obj_factory_wifi_s = NULL; // 工厂WiFi测试窗口
static lv_obj_t* wifi_name_label = NULL; // WiFi名称标签
static lv_obj_t* wifi_signal_img = NULL; // WiFi信号图标
static lv_obj_t* wifi_signal_label = NULL; // WiFi信号强度数字标签
static lv_timer_t* factory_wifi_update_timer = NULL; // WiFi信息更新定时器

bool is_factory_entry_wifi = false;

extern lv_style_t ttf_font_24;

// 按键测试相关
static lv_obj_t* key_test_labels[12]; // 8个按键状态标签
static bool key_test_results[12] = { false }; // 8个按键测试结果
static lv_obj_t* key_test_summary = NULL;

// 焦点相关
static lv_obj_t* focusable_objects[GRID_MAX_OBJECTS];

static void factory_test_key_cb(int key_code, int key_value);
static void factory_test_menu_callback(lv_event_t* e);
static void factory_test_click_callback(lv_obj_t* obj);
static void touch_test_event_handler(lv_event_t* e);
static void key_test_event_handler(lv_event_t* e);
static void wifi_test_event_handler(lv_event_t* e);
static void draw_on_touchpad(lv_event_t* e);
static void check_touch_test_completion(void);
static void touch_test_exit_callback(lv_event_t* e);
static void touch_test_clear_callback(lv_event_t* e);
static void draw_touch_test_points(void);
static void draw_line_to_point(lv_obj_t* canvas, lv_point_t from, lv_point_t to, lv_color_t color);
static void update_touch_test_display(void);
static void init_touch_test_points(void);
static void check_and_connect_point(lv_obj_t* canvas, lv_point_t touch_point);
static void check_line_through_points(lv_obj_t* canvas, lv_point_t from, lv_point_t to);
static void key_test_key_cb(int key_code, int key_value);
static void update_key_test_display(void);
static void key_test_exit_callback(lv_event_t* e);
static void factory_test_Delete_Complete_anim_cb(lv_anim_t* a);
static void factory_test_Delete_anim(void);
static void touch_auto_exit_timer_cb(lv_timer_t* timer);

// 闪光灯测试相关函数声明
static void flash_test_key_cb(int key_code, int key_value);
static void flash_test_event_handler(lv_event_t* e);
static void flash_test_exit_callback(lv_event_t* e);
static void flash_led_set(bool on);
static int32_t flash_led_by_gpio(uint32_t gpio_num, uint32_t pulse, bool is_open);

// 工厂WiFi测试相关函数声明
static void factory_wifi_update_cb(lv_timer_t* t);
static void factory_wifi_key_cb(int key_code, int key_value);
static void factory_wifi_exit_callback(lv_event_t* e);
static int factory_wifi_level_from_dbm(int dbm);

// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################

void factory_test_menu(lv_ui_t* ui)
{
    if (obj_factory_test_s != NULL) {
        if (lv_obj_is_valid(obj_factory_test_s)) {
            MLOG_DBG("obj_factory_test_s 仍然有效，删除旧对象\n");
            lv_obj_del(obj_factory_test_s);
        } else {
            MLOG_DBG("obj_factory_test_s 已被自动销毁，仅重置指针\n");
        }
        obj_factory_test_s = NULL;
    }
    is_factory_entry_wifi = false;
    // 创建主页面容器
    obj_factory_test_s = lv_obj_create(NULL);
    lv_obj_remove_style_all(obj_factory_test_s);
    lv_obj_set_size(obj_factory_test_s, 640, 480);
    lv_obj_set_style_bg_color(obj_factory_test_s, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_center(obj_factory_test_s);
    lv_obj_set_style_bg_opa(obj_factory_test_s, LV_OPA_100, LV_PART_MAIN);

    // 创建顶部栏
    lv_obj_t* top_bar = lv_obj_create(obj_factory_test_s);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 640, 60);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_100, LV_PART_MAIN);

    // 创建返回按钮
    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_set_pos(btn_back, 10, 5);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, factory_test_menu_callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "<");
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label_back);

    // 创建标题
    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "Factory Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 创建设置选项容器
    lv_obj_t* settings_cont = lv_obj_create(obj_factory_test_s);
    lv_obj_remove_style_all(settings_cont);
    lv_obj_set_size(settings_cont, 640, 420);
    lv_obj_set_pos(settings_cont, 0, 60);
    lv_obj_set_style_bg_color(settings_cont, lv_color_hex(0x1A1A1A), LV_PART_MAIN);

    // 定义测试菜单项
    static factory_test_item_t test_items[FACTORY_TEST_ITEMS_COUNT] = {
        { "Touch Screen Test", touch_test_event_handler },
        { "Key Test", key_test_event_handler },
        { "WIFI Test", wifi_test_event_handler },
        { "Flash Test", flash_test_event_handler },
    };

// 按钮样式参数
#define BTN_WIDTH 600
#define BTN_HEIGHT 70
#define BTN_START_Y 20
#define BTN_GAP 10

    // 使用循环创建所有按钮
    for (int i = 0; i < FACTORY_TEST_ITEMS_COUNT; i++) {
        lv_obj_t* btn = lv_btn_create(settings_cont);
        lv_obj_set_size(btn, BTN_WIDTH, BTN_HEIGHT);
        lv_obj_set_pos(btn, 20, BTN_START_Y + i * (BTN_HEIGHT + BTN_GAP));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, test_items[i].event_handler, LV_EVENT_CLICKED, NULL);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, test_items[i].name);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_center(label);

        focusable_objects[i] = btn;
    }

    takephoto_unregister_menu_callback(); //取消注册menu按键
    // 初始化焦点组
    init_focus_group(settings_cont, GRID_COLS, GRID_ROWS, focusable_objects,
        FACTORY_TEST_ITEMS_COUNT, factory_test_click_callback, focusable_objects[0]);
    set_current_page_handler(factory_test_key_cb);
}

void factory_touch_test(lv_ui_t* ui)
{
    MLOG_DBG("factory_touch_test called\n");
    if (lv_obj_is_valid(obj_touch_test_s)) {
        MLOG_DBG("obj_touch_test_s already exists\n");
        return;
    }

    // 创建触摸测试窗口
    obj_touch_test_s = lv_obj_create(NULL);
    lv_obj_remove_style_all(obj_touch_test_s);
    lv_obj_set_size(obj_touch_test_s, 640, 480);
    lv_obj_set_scrollbar_mode(obj_touch_test_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_touch_test_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_touch_test_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj_touch_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj_touch_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    MLOG_DBG("obj_touch_test_s created successfully\n");

    // 创建全屏画布用于显示触摸路径
    touch_canvas = lv_canvas_create(obj_touch_test_s);
    lv_obj_set_size(touch_canvas, 640, 480);
    lv_obj_set_pos(touch_canvas, 0, 0);

// 创建画布绘制缓冲区 (640 * 480 * 2 = 614400 bytes for RGB565)
#define FULLSCREEN_CANVAS_BUF_SIZE (640 * 480 * 2)
    static uint8_t cbuf[FULLSCREEN_CANVAS_BUF_SIZE];
    lv_canvas_set_buffer(touch_canvas, cbuf, 640, 480, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(touch_canvas, lv_color_hex(0x1A1A1A), LV_OPA_COVER);

    // 创建顶部栏背景（半透明）
    touch_overlay = lv_obj_create(obj_touch_test_s);
    lv_obj_remove_style_all(touch_overlay);
    lv_obj_set_size(touch_overlay, 640, 60);
    lv_obj_set_pos(touch_overlay, 0, 0);
    lv_obj_set_style_bg_color(touch_overlay, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(touch_overlay, LV_OPA_0, LV_PART_MAIN);
    lv_obj_clear_flag(touch_overlay, LV_OBJ_FLAG_CLICKABLE);

    // 创建标题
    lv_obj_t* title = lv_label_create(touch_overlay);
    lv_label_set_text(title, "Touch Screen Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 6, 0);

    // 创建提示标签
    touch_test_label = lv_label_create(touch_overlay);
    lv_label_set_text(touch_test_label, "Click or Draw to connect all 9 points");
    lv_obj_set_style_text_font(touch_test_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(touch_test_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
    lv_obj_set_pos(touch_test_label, 100, 45);

    // 创建进度标签
    touch_test_progress = lv_label_create(obj_touch_test_s);
    lv_label_set_text(touch_test_progress, "Progress: 0/9");
    lv_obj_add_style(touch_test_progress, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(touch_test_progress, lv_color_hex(0xFFFF00), LV_PART_MAIN);
    lv_obj_align(touch_test_progress, LV_ALIGN_CENTER, 6, 0);

    // 添加触摸事件到画布
    lv_obj_add_flag(touch_canvas, LV_OBJ_FLAG_CLICKABLE);
    // 注册所有触摸相关事件
    lv_obj_add_event_cb(touch_canvas, draw_on_touchpad, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_canvas, draw_on_touchpad, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(touch_canvas, draw_on_touchpad, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(touch_canvas, draw_on_touchpad, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(touch_canvas, draw_on_touchpad, LV_EVENT_GESTURE, NULL);

    // 初始化9点测试
    init_touch_test_points();
    draw_touch_test_points();

    // 取消焦点导航，使用触摸
    set_current_page_handler(NULL);
}

void factory_key_test(lv_ui_t* ui)
{
    MLOG_DBG("factory_key_test called\n");
    if (lv_obj_is_valid(obj_key_test_s)) {
        MLOG_DBG("obj_key_test_s already exists\n");
        return;
    }

    // 创建按键测试窗口
    obj_key_test_s = lv_obj_create(NULL);
    lv_obj_remove_style_all(obj_key_test_s);
    lv_obj_set_size(obj_key_test_s, 640, 480);
    lv_obj_set_scrollbar_mode(obj_key_test_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_key_test_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_key_test_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj_key_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj_key_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    MLOG_DBG("obj_key_test_s created successfully\n");

    // 创建顶部栏
    lv_obj_t* top_bar = lv_obj_create(obj_key_test_s);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 640, 60);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);

    // 创建返回按钮
    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_set_pos(btn_back, 10, 5);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, key_test_exit_callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "<");
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label_back);

    // 创建标题
    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "Key Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 创建提示标签
    lv_obj_t* hint_label = lv_label_create(obj_key_test_s);
    lv_label_set_text(hint_label, "Press all keys to test");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(hint_label, 20, 70);

    // 创建按键状态标签（显示8个按键的状态）
    const char* key_names[] = { "MENU", "OK", "UP", "DOWN", "LEFT", "RIGHT", "W", "T", "AI", "MODE", "FOCUS", "CAMERA" };

    for (uint8_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        key_test_labels[i] = lv_label_create(obj_key_test_s);
        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s: Not Tested", key_names[i]);
        lv_label_set_text(key_test_labels[i], label_text);
        lv_obj_set_style_text_font(key_test_labels[i], &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(key_test_labels[i], lv_color_hex(0xFF0000), LV_PART_MAIN);

        int row = i / 2;
        int col = i % 2;
        lv_obj_set_pos(key_test_labels[i], 50 + col * 300, 100 + row * 40);
    }

    // 创建测试总结标签
    key_test_summary = lv_label_create(obj_key_test_s);
    lv_label_set_text(key_test_summary, "Test Progress: 0/12");
    lv_obj_set_style_text_font(key_test_summary, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(key_test_summary, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(key_test_summary, 20, 400);

    // 重置测试状态
    for (int i = 0; i < 12; i++) {
        key_test_results[i] = false;
    }
    update_key_test_display();

    // 设置按键处理回调
    set_current_page_handler(key_test_key_cb);
}

// #endregion
// #############################################################################
// ! #region 6. 线程处理函数
// #############################################################################

// #endregion
// #############################################################################
// ! #region 7. 按键、手势、定时器 等事件回调函数
// #############################################################################

static void factory_test_key_cb(int key_code, int key_value)
{
    // 其他按键交给网格导航处理
    handle_grid_navigation(key_code, key_value);
}

static void factory_test_menu_callback(lv_event_t* e)
{
    is_factory_entry_wifi = false;
    MLOG_DBG("factory_test_menu_callback\n");
}

static void factory_test_click_callback(lv_obj_t* obj)
{
    MLOG_DBG("factory_test_click_callback\n");
    lv_obj_t* parent = obj_factory_test_s;
    parent = lv_obj_get_child(parent, 1);
    if (parent == NULL)
        return;
    for (uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if (lv_obj_get_child(parent, i) == obj) {
            if (i == 0) {
                // 触摸划线测试
                if (lv_obj_is_valid(obj_factory_test_s)) {
                    MLOG_DBG("选择屏幕测试\n");
                    ui_load_scr_animation(&g_ui, &obj_touch_test_s, 1, NULL,
                        factory_touch_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, false, true);
                }
            } else if (i == 1) {
                // 按键测试
                if (lv_obj_is_valid(obj_factory_test_s)) {
                    MLOG_DBG("选择按键测试\n");
                    ui_load_scr_animation(&g_ui, &obj_key_test_s, 1, NULL,
                        factory_key_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, false, true);
                }
            } else if (i == 2) {
                if (lv_obj_is_valid(obj_factory_test_s)) {
                    MLOG_DBG("选择wifi测试\n");
                    is_factory_entry_wifi = true;
                    ui_load_scr_animation(&g_ui, &sysMenu_WifiList_s, 1, NULL, sysMenu_WifiList,
                        LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                    // ui_load_scr_animation(&g_ui, &obj_factory_wifi_s, 1, NULL,
                    //     factory_wifi_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, false, true);
                }
            } else if (i == 3) {
                // 闪光灯测试
                if (lv_obj_is_valid(obj_factory_test_s)) {
                    MLOG_DBG("选择闪光灯测试\n");
                    ui_load_scr_animation(&g_ui, &obj_flash_test_s, 1, NULL,
                        factory_flash_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, false, true);
                }
            }
            break;
        }
    }
}

static void touch_test_event_handler(lv_event_t* e)
{
    MLOG_DBG("touch_test_event_handler - Clicked\n");
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("Event code: %d\n", code);
    if (code == LV_EVENT_CLICKED) {
        MLOG_DBG("Entering touch test page\n");
        if (lv_obj_is_valid(obj_factory_test_s)) {
            ui_load_scr_animation(&g_ui, &obj_touch_test_s, 1, NULL,
                factory_touch_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false, true);
        }
    }
}

static void key_test_event_handler(lv_event_t* e)
{
    MLOG_DBG("key_test_event_handler - Clicked\n");
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("Event code: %d\n", code);
    if (code == LV_EVENT_CLICKED) {
        MLOG_DBG("Entering key test page\n");
        if (lv_obj_is_valid(obj_factory_test_s)) {
            ui_load_scr_animation(&g_ui, &obj_key_test_s, 1, NULL,
                factory_key_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false, true);
        }
    }
}

static void wifi_test_event_handler(lv_event_t* e)
{
    MLOG_DBG("wifi_test_event_handler - Clicked\n");
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("Event code: %d\n", code);
    if (code == LV_EVENT_CLICKED) {
        MLOG_DBG("Entering factory wifi test page\n");
        if (lv_obj_is_valid(obj_factory_test_s)) {
            is_factory_entry_wifi = true;
            ui_load_scr_animation(&g_ui, &sysMenu_WifiList_s, 1, NULL, sysMenu_WifiList,
                LV_SCR_LOAD_ANIM_NONE, 20, 20, false, true);
        }
    }
}

static void flash_test_event_handler(lv_event_t* e)
{
    MLOG_DBG("flash_test_event_handler - Clicked\n");
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("Event code: %d\n", code);
    if (code == LV_EVENT_CLICKED) {
        MLOG_DBG("Entering flash test page\n");
        if (lv_obj_is_valid(obj_factory_test_s)) {
            ui_load_scr_animation(&g_ui, &obj_flash_test_s, 1, NULL,
                factory_flash_test, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, false, true);
        }
    }
}

static void draw_on_touchpad(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* canvas = lv_event_get_target(e);

    if (!lv_obj_is_valid(canvas)) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(lv_indev_active(), &point);

    // 边界检查
    if (point.x < 0 || point.x >= 640 || point.y < 0 || point.y >= 480) {
        MLOG_DBG("Ignoring out-of-bound point (%d,%d)\n", point.x, point.y);
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        // 手指按下
        // MLOG_DBG("Touch pressed at (%d,%d), waiting for point %d\n",
        //          point.x, point.y, touch_current_index + 1);

        touch_last_touch_pos = point;
        is_drawing = false; // 重置划线状态

        // 注意：这里不立即检查点连接，避免划线开始时误触测试点导致中断
        // 只在点击方式下才检查点连接，划线方式在移动过程中检查

    } else if (code == LV_EVENT_PRESSING || code == LV_EVENT_GESTURE) {
        // 手指移动中（划线方式）
        if (point.x == touch_last_touch_pos.x && point.y == touch_last_touch_pos.y) {
            return;
        }

        // 绘制划线轨迹
        draw_line_to_point(canvas, touch_last_touch_pos, point, lv_color_hex(0x00FF00));

        // 标记正在进行划线操作
        is_drawing = true;

        // 检查划线是否经过未连接的点（添加防误触保护）
        check_line_through_points(canvas, touch_last_touch_pos, point);

        touch_last_touch_pos = point;

    } else if (code == LV_EVENT_RELEASED) {
        // 手指释放
        MLOG_DBG("Touch released at (%d,%d)\n", point.x, point.y);

        // 检查是否为点击操作（释放点与按下点距离很近）
        int dx = point.x - touch_last_touch_pos.x;
        int dy = point.y - touch_last_touch_pos.y;
        int distance = (int)sqrt((float)(dx * dx + dy * dy));

        if (distance < 10 && !is_drawing) { // 如果移动距离很小且不是划线操作，认为是点击操作
            MLOG_DBG("Click detected at (%d,%d), checking point connection\n", point.x, point.y);
            check_and_connect_point(canvas, point);
        }

        // 重置划线状态
        is_drawing = false;

        // 重置提示颜色
        if (touch_skipped_count < TOUCH_TEST_POINT_COUNT) {
            lv_label_set_text(touch_test_label, "Click or Draw to connect all 9 points");
            lv_obj_set_style_text_color(touch_test_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        }
    }
}

// 检查并连接点（点击方式）- 乱序支持，任意点击即可
static void check_and_connect_point(lv_obj_t* canvas, lv_point_t touch_point)
{
    // 检查所有未连接的点
    for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
        if (touch_point_tested[i])
            continue; // 已连接的跳过

        lv_point_t target = touch_test_points[i];
        int dx = touch_point.x - target.x;
        int dy = touch_point.y - target.y;
        int distance = (int)sqrt((float)(dx * dx + dy * dy));

        if (distance <= TOUCH_TEST_TOLERANCE) {
            // 触摸到了点i，标记为已连接
            MLOG_DBG("Clicked point %d\n", i + 1);

            // 绘制点的连线效果
            draw_line_to_point(canvas,
                touch_test_points[i],
                touch_test_points[i],
                lv_color_hex(0x00FF00));

            touch_point_tested[i] = true;
            touch_skipped_count++;

            // 更新显示
            update_touch_test_display();

            // 检查是否全部完成（所有点都已连接）
            check_touch_test_completion();
            return;
        }
    }
}

// 检查线段是否经过未连接的点（划线方式）- 乱序支持
static void check_line_through_points(lv_obj_t* canvas, lv_point_t from, lv_point_t to)
{
    // 使用Bresenham算法检查线段经过的所有像素点
    int x0 = from.x;
    int y0 = from.y;
    int x1 = to.x;
    int y1 = to.y;

    // 避免检查起始点本身，防止划线开始时误触测试点
    // 只有当线段长度足够时才进行检查
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    if (dx < 20 && dy < 20) {
        return; // 线段太短，不检查（避免划线开始时的误触）
    }

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    bool any_point_connected = false;

    // 跳过前30%的线段，避免划线开始时误触
    int skip_steps = (dx + dy) / 3; // 跳过总长度的30%
    if (skip_steps < 1)
        skip_steps = 1;

    for (int step = 0; step < skip_steps; step++) {
        if (x0 == x1 && y0 == y1)
            return;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    while (1) {
        // 检查当前像素点是否在任意未连接的目标点容差范围内
        for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
            if (touch_point_tested[i])
                continue; // 已连接的跳过

            lv_point_t target = touch_test_points[i];
            int dx_p = x0 - target.x;
            int dy_p = y0 - target.y;
            int dist = (int)sqrt((float)(dx_p * dx_p + dy_p * dy_p));

            if (dist <= TOUCH_TEST_TOLERANCE) {
                // 划线经过任意点 - 直接连接
                MLOG_DBG("Line passed through point %d\n", i + 1);

                // 绘制点的连线效果
                draw_line_to_point(canvas,
                    touch_test_points[i],
                    touch_test_points[i],
                    lv_color_hex(0x00FF00));

                touch_point_tested[i] = true;
                touch_skipped_count++;
                any_point_connected = true;
            }
        }

        // Bresenham算法步进
        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }

    if (any_point_connected) {
        // 更新显示
        update_touch_test_display();

        // 检查是否全部完成（所有点都已连接）
        check_touch_test_completion();
    }
}

// 触摸测试自动退出定时器
static lv_timer_t* touch_auto_exit_timer = NULL;

static void touch_auto_exit_timer_cb(lv_timer_t* timer)
{
    if (touch_auto_exit_timer) {
        lv_timer_del(touch_auto_exit_timer);
        touch_auto_exit_timer = NULL;
    }

    // 清理所有点标签
    for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
        if (touch_point_labels[i] && lv_obj_is_valid(touch_point_labels[i])) {
            lv_obj_del(touch_point_labels[i]);
            touch_point_labels[i] = NULL;
        }
    }

    touch_canvas = NULL;
    touch_overlay = NULL;
    touch_test_label = NULL;
    touch_test_progress = NULL;

    // 重置测试状态
    touch_skipped_count = 0;
    is_drawing = false;
    memset(touch_point_tested, 0, sizeof(touch_point_tested));

    set_current_page_handler(NULL);

    // 返回工厂测试菜单
    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false, true);
}

static void check_touch_test_completion(void)
{
    // 计算已连接的点数
    int connected_count = 0;
    for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
        if (touch_point_tested[i]) {
            connected_count++;
        }
    }

    // 更新进度显示
    if (touch_test_progress) {
        char progress_text[64];
        snprintf(progress_text, sizeof(progress_text), "Progress: %d/9", connected_count);
        if (connected_count == TOUCH_TEST_POINT_COUNT) {
            lv_label_set_text(touch_test_progress, "PASS - All 9 Points!");
            lv_obj_set_style_text_color(touch_test_progress, lv_color_hex(0x00FF00), LV_PART_MAIN);
        } else {
            lv_label_set_text(touch_test_progress, progress_text);
            lv_obj_set_style_text_color(touch_test_progress, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        }
    }

    // 更新提示
    if (touch_test_label) {
        if (connected_count == TOUCH_TEST_POINT_COUNT) {
            // 所有点都已连接，测试完成，显示自动退出提示
            lv_label_set_text(touch_test_label, "Test PASS! Auto exit in 1s...");
            lv_obj_set_style_text_color(touch_test_label, lv_color_hex(0x00FF00), LV_PART_MAIN);

            // 启动1秒定时器后自动退出
            if (touch_auto_exit_timer == NULL) {
                touch_auto_exit_timer = lv_timer_create(touch_auto_exit_timer_cb, 1000, NULL);
            }

            MLOG_DBG("Touch test completed successfully! Auto exit in 1s\n");
        } else {
            // 正常提示剩余点数
            char hint[64];
            snprintf(hint, sizeof(hint), "Connect %d more point(s)", TOUCH_TEST_POINT_COUNT - connected_count);
            lv_label_set_text(touch_test_label, hint);
            lv_obj_set_style_text_color(touch_test_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        }
    }
}

static void touch_test_clear_callback(lv_event_t* e)
{
    MLOG_DBG("touch_test_clear_callback\n");

    // 删除自动退出定时器
    if (touch_auto_exit_timer) {
        lv_timer_del(touch_auto_exit_timer);
        touch_auto_exit_timer = NULL;
    }

    if (touch_canvas && lv_obj_is_valid(touch_canvas)) {
        // 清除画布
        lv_canvas_fill_bg(touch_canvas, lv_color_hex(0x1A1A1A), LV_OPA_COVER);

        // 重置测试状态
        touch_skipped_count = 0;
        is_drawing = false;
        memset(touch_point_tested, 0, sizeof(touch_point_tested));

        // 重新绘制测试点
        init_touch_test_points();
        draw_touch_test_points();

        if (touch_test_progress) {
            lv_label_set_text(touch_test_progress, "Progress: 0/9");
            lv_obj_set_style_text_color(touch_test_progress, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        }
        if (touch_test_label) {
            lv_label_set_text(touch_test_label, "Click or Draw to connect all 9 points");
            lv_obj_set_style_text_color(touch_test_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
        }

        MLOG_DBG("Touch test reset\n");
    }
}

static void touch_test_exit_callback(lv_event_t* e)
{
    MLOG_DBG("touch_test_exit_callback\n");

    // 删除自动退出定时器
    if (touch_auto_exit_timer) {
        lv_timer_del(touch_auto_exit_timer);
        touch_auto_exit_timer = NULL;
    }

    // 清理所有点标签
    for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
        if (touch_point_labels[i] && lv_obj_is_valid(touch_point_labels[i])) {
            lv_obj_del(touch_point_labels[i]);
            touch_point_labels[i] = NULL;
        }
    }

    touch_canvas = NULL;
    touch_overlay = NULL;
    touch_test_label = NULL;
    touch_test_progress = NULL;

    // 重置测试状态
    touch_skipped_count = 0;
    is_drawing = false;
    memset(touch_point_tested, 0, sizeof(touch_point_tested));

    set_current_page_handler(NULL);

    // 返回工厂测试菜单
    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false, true);
}

static void key_test_key_cb(int key_code, int key_value)
{
    // 处理 MENU 键退出
    // if (key_code == KEY_MENU && key_value == 1) {
    //     if (lv_obj_is_valid(obj_key_test_s)) {
    //         key_test_exit_callback(NULL);
    //     }
    //     return;
    // }

    if (!lv_obj_is_valid(obj_key_test_s) || key_value != 1) {
        return;
    }

    const char* key_names[] = { "MENU", "OK", "UP", "DOWN", "LEFT", "RIGHT", "W", "T", "AI", "MODE", "FOCUS", "CAMERA" };

    int key_index = -1;
    switch (key_code) {
    case KEY_MENU:
        key_index = 0;
        break;
    case KEY_OK:
        key_index = 1;
        break;
    case KEY_UP:
        key_index = 2;
        break;
    case KEY_DOWN:
        key_index = 3;
        break;
    case KEY_LEFT:
        key_index = 4;
        break;
    case KEY_RIGHT:
        key_index = 5;
        break;
    case KEY_ZOOMOUT:
        key_index = 6;
        break;
    case KEY_ZOOMIN:
        key_index = 7;
        break;
    case KEY_PLAY:
        key_index = 8;
        break;
    case KEY_MODE:
        key_index = 9;
        break;
    case KEY_CAMERA_FOCUS:
        key_index = 10;
        break;
    case KEY_CAMERA:
        key_index = 11;
        break;
    default:
        return;
    }

    if (key_index >= 0 && key_index < 12 && !key_test_results[key_index]) {
        key_test_results[key_index] = true;
        update_key_test_display();

        MLOG_DBG("Key %s tested\n", key_names[key_index]);
    }
}

static void update_key_test_display(void)
{
    const char* key_names[] = { "MENU", "OK", "UP", "DOWN", "LEFT", "RIGHT", "W", "T", "AI", "MODE", "FOCUS", "CAMERA" };

    // 更新每个按键的显示
    for (uint8_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
        if (key_test_labels[i]) {
            char label_text[64];
            if (key_test_results[i]) {
                snprintf(label_text, sizeof(label_text), "%s: Tested", key_names[i]);
                lv_obj_set_style_text_color(key_test_labels[i], lv_color_hex(0x00FF00), LV_PART_MAIN);
            } else {
                snprintf(label_text, sizeof(label_text), "%s: Not Tested", key_names[i]);
                lv_obj_set_style_text_color(key_test_labels[i], lv_color_hex(0xFF0000), LV_PART_MAIN);
            }
            lv_label_set_text(key_test_labels[i], label_text);
        }
    }

    // 更新测试总结
    if (key_test_summary) {
        int tested_count = 0;
        for (uint8_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
            if (key_test_results[i]) {
                tested_count++;
            }
        }

        char summary_text[64];
        snprintf(summary_text, sizeof(summary_text), "Test Progress: %d/12", tested_count);
        lv_label_set_text(key_test_summary, summary_text);

        // 如果全部测试完成，显示成功信息
        if (tested_count == 12) {
            lv_obj_set_style_text_color(key_test_summary, lv_color_hex(0x00FF00), LV_PART_MAIN);
        }
    }
}

static void key_test_exit_callback(lv_event_t* e)
{
    set_current_page_handler(NULL);
    // 返回工厂测试菜单
    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false, true);
}

// 初始化9点测试点位置
// 采用3x3蛇形网格分布，覆盖整个屏幕有效区域（避开顶部60px标题栏）
// 9个点可以任意顺序点击，无需按顺序
static void init_touch_test_points(void)
{
    // 屏幕可用区域：x: 40-600, y: 90-420
    // 3x3蛇形网格分布，9个点按蛇形顺序排列
    // 第1行(左到右): 左上角 → 上中 → 右上角
    // 第2行(右到左): 右中 → 正中 → 左中
    // 第3行(左到右): 左下角 → 下中 → 右下角

    touch_test_points[0].x = 40;
    touch_test_points[0].y = 20; // 点1 左上角
    touch_test_points[1].x = 320;
    touch_test_points[1].y = 20; // 点2 上中
    touch_test_points[2].x = 600;
    touch_test_points[2].y = 20; // 点3 右上角
    touch_test_points[3].x = 600;
    touch_test_points[3].y = 220; // 点4 右中
    touch_test_points[4].x = 320;
    touch_test_points[4].y = 220; // 点5 正中
    touch_test_points[5].x = 40;
    touch_test_points[5].y = 220; // 点6 左中
    touch_test_points[6].x = 40;
    touch_test_points[6].y = 450; // 点7 左下角
    touch_test_points[7].x = 320;
    touch_test_points[7].y = 450; // 点8 下中
    touch_test_points[8].x = 600;
    touch_test_points[8].y = 450; // 点9 右下角

    // 重置测试状态
    touch_skipped_count = 0;
    is_drawing = false;
    memset(touch_point_tested, 0, sizeof(touch_point_tested));
}

// 在画布上绘制9个测试点和编号
static void draw_touch_test_points(void)
{
    if (!touch_canvas || !lv_obj_is_valid(touch_canvas)) {
        return;
    }

    lv_obj_t* canvas = touch_canvas;

    // 清除并重绘背景
    lv_canvas_fill_bg(canvas, lv_color_hex(0x1A1A1A), LV_OPA_COVER);

    // 绘制每个测试点
    for (int i = 0; i < TOUCH_TEST_POINT_COUNT; i++) {
        lv_point_t pt = touch_test_points[i];

        // 选择颜色：已连接为绿色，未连接为黄色
        uint32_t color_hex;
        if (touch_point_tested[i]) {
            color_hex = 0x00FF00; // 已连接 - 绿色
        } else {
            color_hex = 0xFFFF00; // 未连接 - 黄色
        }

        // 绘制点圆圈
        for (int r = 15; r <= 20; r++) {
            for (int angle = 0; angle < 360; angle += 5) {
                int px = pt.x + (int)(r * cosf(angle * 3.14159f / 180.0f));
                int py = pt.y + (int)(r * sinf(angle * 3.14159f / 180.0f));
                if (px >= 0 && px < 640 && py >= 0 && py < 480) {
                    lv_canvas_set_px(canvas, px, py, lv_color_hex(color_hex), LV_OPA_COVER);
                }
            }
        }

        // 绘制点的中心实心圆
        for (int dx = -6; dx <= 6; dx++) {
            for (int dy = -6; dy <= 6; dy++) {
                if (dx * dx + dy * dy <= 36) { // 圆形区域
                    int px = pt.x + dx;
                    int py = pt.y + dy;
                    if (px >= 0 && px < 640 && py >= 0 && py < 480) {
                        lv_canvas_set_px(canvas, px, py, lv_color_hex(color_hex), LV_OPA_COVER);
                    }
                }
            }
        }

        // 绘制点的编号（在点右侧，避免角落位置被遮挡）
        if (touch_point_labels[i] && lv_obj_is_valid(touch_point_labels[i])) {
            lv_obj_del(touch_point_labels[i]);
            touch_point_labels[i] = NULL;
        }

        touch_point_labels[i] = lv_label_create(obj_touch_test_s);
        char num_text[8];
        snprintf(num_text, sizeof(num_text), "%d", i + 1);
        lv_label_set_text(touch_point_labels[i], num_text);
        lv_obj_set_style_text_font(touch_point_labels[i], &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(touch_point_labels[i], lv_color_hex(color_hex), LV_PART_MAIN);

        // 标签位置：点在右侧，避免超出边界
        int label_x = pt.x + 25;
        if (label_x > 600)
            label_x = 600; // 防止超出右边界
        if (label_x < 10)
            label_x = 10; // 防止超出左边界
        lv_obj_set_pos(touch_point_labels[i], label_x, pt.y - 10);
    }
}

// 绘制从from到to的线段
static void draw_line_to_point(lv_obj_t* canvas, lv_point_t from, lv_point_t to, lv_color_t color)
{
    int x0 = from.x;
    int y0 = from.y;
    int x1 = to.x;
    int y1 = to.y;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int max_distance = (dx > dy) ? dx : dy;

    // 确保线条连续
    int steps = (max_distance <= 0) ? 1 : max_distance * 2;
    if (steps < 1)
        steps = 1;
    if (steps > 500)
        steps = 500;

    for (int step = 0; step <= steps; step++) {
        float t = (float)step / steps;
        int cx = (int)(x0 + (x1 - x0) * t + 0.5f);
        int cy = (int)(y0 + (y1 - y0) * t + 0.5f);

        if (cx < 0 || cx >= 640 || cy < 0 || cy >= 480) {
            continue;
        }

        // 绘制粗线条
        for (int ox = -3; ox <= 3; ox++) {
            for (int oy = -3; oy <= 3; oy++) {
                int px = cx + ox;
                int py = cy + oy;
                if (px >= 0 && px < 640 && py >= 0 && py < 480) {
                    lv_canvas_set_px(canvas, px, py, color, LV_OPA_COVER);
                }
            }
        }
    }
}

// 更新测试显示
static void update_touch_test_display(void)
{
    // 重绘所有点以更新颜色
    draw_touch_test_points();
}

// #############################################################################
// ! #region 闪光灯GPIO控制函数
// #############################################################################

/**
 * @brief 通过GPIO控制闪光灯
 * @param gpio_num GPIO编号
 * @param pulse 脉冲次数 (1~16)
 * @param is_open true=打开闪光灯, false=关闭闪光灯
 * @return 0=成功, -1=失败
 */
static int32_t flash_led_by_gpio(uint32_t gpio_num, uint32_t pulse, bool is_open)
{
    uint32_t i = 0;
    uint32_t j = -1;
    int32_t fd = -1;
    char path[64] = { 0 };

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        MLOG_ERR("open /sys/class/gpio/export failed\n");
        return -1;
    }
    snprintf(path, sizeof(path), "%d", gpio_num);
    write(fd, path, strlen(path));
    close(fd);

    memset(path, 0, sizeof(path));
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio_num);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        MLOG_ERR("open gpio direction failed\n");
        return -1;
    }
    write(fd, "out", 3);
    close(fd);

    memset(path, 0, sizeof(path));
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_num);
    fd = open(path, O_RDWR);
    if (fd < 0) {
        MLOG_ERR("open gpio value failed\n");
        return -1;
    }

    if (is_open) {
        for (i = 0; i < pulse; i++) {
            lseek(fd, 0, SEEK_SET);
            write(fd, "0", 1);
            for (j = 0; j < 500; j++)
                ;
            lseek(fd, 0, SEEK_SET);
            write(fd, "1", 1);
            for (j = 0; j < 500; j++)
                ;
        }
    } else {
        write(fd, "0", 1);
    }

    close(fd);

    return 0;
}

/**
 * @brief 设置闪光灯开关状态
 * @param on true=打开, false=关闭
 */
static void flash_led_set(bool on)
{
    flash_led_by_gpio(FLASH_LED_GPIO_NUM, FLASH_LED_PULSE, on);
    MLOG_DBG("Flash LED %s\n", on ? "ON" : "OFF");
}

// #############################################################################
// ! #region 闪光灯测试功能
// #############################################################################

// 闪光灯测试页面主函数
void factory_flash_test(lv_ui_t* ui)
{
    MLOG_DBG("factory_flash_test called\n");
    if (lv_obj_is_valid(obj_flash_test_s)) {
        MLOG_DBG("obj_flash_test_s already exists\n");
        return;
    }

    // 创建闪光灯测试窗口
    obj_flash_test_s = lv_obj_create(NULL);
    lv_obj_remove_style_all(obj_flash_test_s);
    lv_obj_set_size(obj_flash_test_s, 640, 480);
    lv_obj_set_scrollbar_mode(obj_flash_test_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_flash_test_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_flash_test_s, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj_flash_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj_flash_test_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 创建顶部栏
    lv_obj_t* top_bar = lv_obj_create(obj_flash_test_s);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 640, 60);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);

    // 创建返回按钮
    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_set_pos(btn_back, 10, 5);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, flash_test_exit_callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "<");
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label_back);

    // 创建标题
    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "Flash Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 创建提示标签
    lv_obj_t* hint_label = lv_label_create(obj_flash_test_s);
    lv_label_set_text(hint_label, "Press UP to toggle flash ON/OFF");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(hint_label, 20, 80);

    // 创建状态显示区域
    lv_obj_t* status_bg = lv_obj_create(obj_flash_test_s);
    lv_obj_remove_style_all(status_bg);
    lv_obj_set_size(status_bg, 300, 150);
    lv_obj_align(status_bg, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_color(status_bg, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_radius(status_bg, 20, LV_PART_MAIN);

    // 创建状态标签
    flash_test_label = lv_label_create(status_bg);
    lv_label_set_text(flash_test_label, "OFF");
    lv_obj_set_style_text_font(flash_test_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(flash_test_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_center(flash_test_label);

    // 创建操作提示
    lv_obj_t* hint_label2 = lv_label_create(obj_flash_test_s);
    lv_label_set_text(hint_label2, "UP: Toggle Flash | MENU: Exit");
    lv_obj_set_style_text_font(hint_label2, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label2, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(hint_label2, LV_ALIGN_CENTER, 0, 80);

    // 初始化闪光灯状态为关闭
    flash_is_on = false;
    flash_led_set(false);

    // 设置按键处理回调
    set_current_page_handler(flash_test_key_cb);
}

// 闪光灯测试按键回调
static void flash_test_key_cb(int key_code, int key_value)
{
    if (!lv_obj_is_valid(obj_flash_test_s) || key_value != 1) {
        return;
    }

    switch (key_code) {
    case KEY_UP: {
        // 切换闪光灯状态
        flash_is_on = !flash_is_on;
        flash_led_set(flash_is_on);

        // 更新显示
        if (flash_test_label && lv_obj_is_valid(flash_test_label)) {
            if (flash_is_on) {
                lv_label_set_text(flash_test_label, "ON");
                lv_obj_set_style_text_color(flash_test_label, lv_color_hex(0x00FF00), LV_PART_MAIN);
            } else {
                lv_label_set_text(flash_test_label, "OFF");
                lv_obj_set_style_text_color(flash_test_label, lv_color_hex(0xFF0000), LV_PART_MAIN);
            }
        }
        MLOG_DBG("Flash %s\n", flash_is_on ? "ON" : "OFF");
        break;
    }
    case KEY_MENU: {
        // 返回工厂测试菜单
        flash_test_exit_callback(NULL);
        break;
    }
    default:
        break;
    }
}

// 闪光灯测试退出回调
static void flash_test_exit_callback(lv_event_t* e)
{
    MLOG_DBG("flash_test_exit_callback\n");

    // 确保闪光灯关闭
    flash_led_set(false);

    set_current_page_handler(NULL);

    // 返回工厂测试菜单
    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false, true);
}

// #############################################################################
// ! #region 工厂WiFi测试功能
// #############################################################################

// WiFi信号强度等级转换
static int factory_wifi_level_from_dbm(int dbm)
{
    static int wifi_level = 0;
    if (dbm == -1)
        wifi_level = 0;
    else if (dbm >= -50)
        wifi_level = 4; // >=-50: 满格
    else if (dbm >= -60)
        wifi_level = 3; // -60~-50: 3格
    else if (dbm >= -70)
        wifi_level = 2; // -70~-60: 2格
    else if (dbm >= -80)
        wifi_level = 1; // -80~-70: 1格
    else if (dbm < -80)
        wifi_level = 1;
    return wifi_level;
}

// 工厂WiFi信息更新回调
static void factory_wifi_update_cb(lv_timer_t* t)
{
    int wifi_enabled = 0;
    int wifi_connected = 0;
    int wifi_signal = -1;
    int level = 0;
    // char ssid[64] = { 0 };

    UNUSED(t);

    wifi_manager_poll();
    param_manager_poll();
    wifi_enabled = param_manager_get(PARAM_ID_WIFI_ENABLED);
    wifi_connected = param_manager_get(PARAM_ID_WIFI_CONNECTED);
    wifi_signal = param_manager_get(PARAM_ID_WIFI_SIGNAL_DBM);
    level = factory_wifi_level_from_dbm(wifi_signal);

    // 更新WiFi名称显示
    if (wifi_name_label != NULL && lv_obj_is_valid(wifi_name_label)) {
        if (wifi_enabled && wifi_connected) {
            // 尝试获取已连接的SSID
            const char* connected_ssid = wifi_manager_get_connected_ssid();
            if (connected_ssid != NULL && strlen(connected_ssid) > 0) {
                char ssid[64] = { 0 };
                snprintf(ssid, sizeof(ssid), "名称: %s", connected_ssid);
                lv_label_set_text(wifi_name_label, ssid);
                lv_obj_set_style_text_color(wifi_name_label, lv_color_hex(0x00FF00), LV_PART_MAIN);
            } else {
                lv_label_set_text(wifi_name_label, "Connecting...");
                lv_obj_set_style_text_color(wifi_name_label, lv_color_hex(0xFFFF00), LV_PART_MAIN);
            }
        } else if (wifi_enabled && !wifi_connected) {
            lv_label_set_text(wifi_name_label, "WiFi: Not Connected");
            lv_obj_set_style_text_color(wifi_name_label, lv_color_hex(0xFFA500), LV_PART_MAIN);
        } else {
            lv_label_set_text(wifi_name_label, "WiFi: Disabled");
            lv_obj_set_style_text_color(wifi_name_label, lv_color_hex(0x888888), LV_PART_MAIN);
        }
    }

    // 更新WiFi信号图标
    if (wifi_signal_img != NULL && lv_obj_is_valid(wifi_signal_img)) {
        if (wifi_enabled && wifi_connected && level > 0) {
            lv_img_set_src(wifi_signal_img, level == 1 ? &_wifi_1_RGB565A8_24x23 : level == 2 ? &_wifi_2_RGB565A8_24x23
                    : level == 3                                                              ? &_wifi_3_RGB565A8_24x23
                                                                                              : &_wifi_4_RGB565A8_24x23);
        } else {
            lv_img_set_src(wifi_signal_img, "F:/sys-wifi.png");
        }
    }

    // 更新WiFi信号强度数字
    if (wifi_signal_label != NULL && lv_obj_is_valid(wifi_signal_label)) {
        char signal_text[32];
        if (wifi_enabled && wifi_connected && wifi_signal != -1) {
            snprintf(signal_text, sizeof(signal_text), "%d dBm", wifi_signal);
            lv_obj_set_style_text_color(wifi_signal_label, lv_color_hex(0x00FF00), LV_PART_MAIN);
        } else if (wifi_enabled && !wifi_connected) {
            snprintf(signal_text, sizeof(signal_text), "-- dBm");
            lv_obj_set_style_text_color(wifi_signal_label, lv_color_hex(0xFFA500), LV_PART_MAIN);
        } else {
            snprintf(signal_text, sizeof(signal_text), "N/A");
            lv_obj_set_style_text_color(wifi_signal_label, lv_color_hex(0x888888), LV_PART_MAIN);
        }
        lv_label_set_text(wifi_signal_label, signal_text);
    }
}

// 工厂WiFi测试按键回调
static void factory_wifi_key_cb(int key_code, int key_value)
{
    if (!lv_obj_is_valid(obj_factory_wifi_s) || key_value != 1) {
        return;
    }

    switch (key_code) {
    case KEY_MENU: {
        // 返回工厂测试菜单
        factory_wifi_exit_callback(NULL);
        break;
    }
    default:
        break;
    }
}

// 工厂WiFi测试退出回调
static void factory_wifi_exit_callback(lv_event_t* e)
{
    MLOG_DBG("factory_wifi_exit_callback\n");

    // 删除更新定时器
    if (factory_wifi_update_timer != NULL) {
        lv_timer_del(factory_wifi_update_timer);
        factory_wifi_update_timer = NULL;
    }

    wifi_name_label = NULL;
    wifi_signal_img = NULL;
    wifi_signal_label = NULL;

    set_current_page_handler(NULL);

    // 返回工厂测试菜单
    ui_load_scr_animation(&g_ui, &obj_factory_test_s, 1, NULL, factory_test_menu,
        LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false, true);
}

// 工厂WiFi测试页面
void factory_wifi_test(lv_ui_t* ui)
{
    MLOG_DBG("factory_wifi_test called\n");
    if (lv_obj_is_valid(obj_factory_wifi_s)) {
        MLOG_DBG("obj_factory_wifi_s already exists\n");
        return;
    }

    // 创建工厂WiFi测试窗口
    obj_factory_wifi_s = lv_obj_create(NULL);
    lv_obj_remove_style_all(obj_factory_wifi_s);
    lv_obj_set_size(obj_factory_wifi_s, 640, 480);
    lv_obj_set_scrollbar_mode(obj_factory_wifi_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(obj_factory_wifi_s, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_factory_wifi_s, lv_color_hex(0x1A1A1A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj_factory_wifi_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj_factory_wifi_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 创建顶部栏
    lv_obj_t* top_bar = lv_obj_create(obj_factory_wifi_s);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 640, 60);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);

    // 创建返回按钮
    lv_obj_t* btn_back = lv_btn_create(top_bar);
    lv_obj_set_size(btn_back, 80, 50);
    lv_obj_set_pos(btn_back, 10, 5);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, factory_wifi_exit_callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "<");
    lv_obj_set_style_text_font(label_back, &lv_font_montserrat_34, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label_back);

    // 创建标题
    lv_obj_t* title = lv_label_create(top_bar);
    lv_label_set_text(title, "WiFi Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 创建WiFi信息显示容器
    lv_obj_t* info_container = lv_obj_create(obj_factory_wifi_s);
    lv_obj_remove_style_all(info_container);
    lv_obj_set_size(info_container, 500, 300);
    lv_obj_align(info_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_radius(info_container, 20, LV_PART_MAIN);

    // WiFi图标
    lv_obj_t* wifi_icon = lv_img_create(info_container);
    lv_img_set_src(wifi_icon, "F:/sys-wifi.png");
    lv_obj_align(wifi_icon, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_image_recolor(wifi_icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(wifi_icon, LV_OPA_COVER, LV_PART_MAIN);

    // WiFi名称标签
    wifi_name_label = lv_label_create(info_container);
    lv_label_set_text(wifi_name_label, "Loading...");
    lv_obj_add_style(wifi_name_label, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(wifi_name_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(wifi_name_label, LV_ALIGN_TOP_MID, 0, 100);

    // 信号强度区域
    lv_obj_t* signal_container = lv_obj_create(info_container);
    lv_obj_remove_style_all(signal_container);
    lv_obj_set_size(signal_container, 400, 100);
    lv_obj_align(signal_container, LV_ALIGN_BOTTOM_MID, 0, 20);
    lv_obj_set_style_bg_opa(signal_container, LV_OPA_0, LV_PART_MAIN);

    // 信号强度图标
    wifi_signal_img = lv_img_create(signal_container);
    lv_img_set_src(wifi_signal_img, "F:/sys-wifi.png");
    lv_obj_align(wifi_signal_img, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_image_recolor(wifi_signal_img, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(wifi_signal_img, LV_OPA_COVER, LV_PART_MAIN);

    // 信号强度标签
    wifi_signal_label = lv_label_create(signal_container);
    lv_label_set_text(wifi_signal_label, "-- dBm");
    lv_obj_set_style_text_font(wifi_signal_label, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(wifi_signal_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align_to(wifi_signal_label, wifi_signal_img, LV_ALIGN_OUT_RIGHT_MID, 40, 0);

    // 创建操作提示
    lv_obj_t* hint_label = lv_label_create(obj_factory_wifi_s);
    lv_label_set_text(hint_label, "MENU: Exit");
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // 如果WiFi未开启，自动开启WiFi
    int wifi_enabled = param_manager_get(PARAM_ID_WIFI_ENABLED);
    if (!wifi_enabled) {
        MLOG_INFO("WiFi disabled, enabling WiFi for factory test...\n");
        wifi_manager_set_enabled(1);
    }

    // 初始化WiFi信息
    factory_wifi_update_cb(NULL);

    // 创建定时器，每秒更新一次WiFi信息
    if (factory_wifi_update_timer == NULL) {
        factory_wifi_update_timer = lv_timer_create(factory_wifi_update_cb, 1000, NULL);
        lv_timer_ready(factory_wifi_update_timer);
    }

    // 设置按键处理回调
    set_current_page_handler(factory_wifi_key_cb);
}

// #endregion
// #############################################################################
// ! #region 8. 初始化、去初始化、资源管理
// #############################################################################

// #endregion
