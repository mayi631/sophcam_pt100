#define DEBUG
#include "common/extract_thumbnail.h"
#include "common/takephoto.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "image_recognize/image_recognize.h"
#include "indev.h"
#include "lvgl.h"
#include "mlog.h"
#include "page_all.h"
#include "ttp.h"
#include "ui_common.h"
#include "wifi_img.h"
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESIZE_IMG_PATH "/mnt/data/bin/res/img_resized.jpg"

/* 滚动相关变量 */
static int current_scroll_pos = 0; // 当前滚动位置
static int max_scroll_pos = 0; // 最大滚动位置

/* 全局变量声明 */
extern lv_style_t ttf_font_30;

static char pic_thumbnail[128] = { 0 };
// 底层页面对象
lv_obj_t* page_ai_shoot_translation_s = NULL;
//翻译预览文本框控件对象
static lv_obj_t* translation_preview_s = NULL;

// static lv_timer_t* auto_tranfailure_close = NULL;

// 翻译结果文本
static const char *result_text = NULL;
// 结果页面描述标签
static lv_obj_t* result_desc_label = NULL;
// 翻译结果更新定时器
static lv_timer_t* update_result_timer = NULL;
static lv_timer_t* read_status_timer = NULL;
static lv_obj_t* voice_man = NULL;
//音量设置变量
static lv_obj_t* text_cont = NULL;
static lv_obj_t* corner_deco = NULL;

// 在全局变量声明区域添加长按相关变量
static lv_timer_t *up_long_press_timer = NULL;
static bool up_long_press_flag = false;
static lv_timer_t* down_long_press_timer = NULL;
static bool down_long_press_flag = false;

//重新朗读文本
static lv_obj_t* s_reread_btn = NULL;
static lv_obj_t* s_reread_label = NULL;
//是否正在朗读
static bool s_is_read = false;

// 图像识别线程相关变量
static pthread_t g_recognize_thread = 0; // 识别线程
static volatile bool g_recognize_cancelled = false; // 取消标志
static volatile bool g_recognize_running = false; // 识别运行标志
static volatile bool g_recognize_completed = false; // 识别完成标志
static char g_recognize_result[4096] = { 0 }; // 识别结果缓冲区
static char g_recognize_image_path[256] = { 0 }; // 待识别图片路径
static pthread_mutex_t g_recognize_mutex = PTHREAD_MUTEX_INITIALIZER; // 线程互斥锁
static int g_recognize_id = 0; // 识别任务唯一ID，用于识别过期回调
static volatile bool g_cancelled_result = false; // 取消标记，用于区分取消和失败
// 线程函数声明
static void* recognize_image_thread(void* arg);
static void recognize_result_ready(void* data);

/* 函数声明 */
/* 显示翻译结果 */
static void translation_show_result(void);
/* 翻译结果更新定时器回调函数 */
static void translation_update_result_cb(lv_timer_t *timer);
/*翻译完成页面返回*/
static void translation_back_btn_result_cb(lv_event_t *e);
/*底层页面返回*/
static void translation_back_cb(lv_event_t *e);
// 菜单按键处理回调函数
static void translation_menu_callback(void);
static void translation_result_key_handler(int key_code, int key_value);
//音量设置相关
static void voice_value_btn_event_cb(lv_event_t *e);
static void wifi_return_to_translation(void *user_data);

// 放大按键事件处理
static void zoomin_key_cb(void);
// 缩小按键事件处理
static void zoomout_key_cb(void);

static void voice_style_btn_event_cb(lv_event_t *e);

//快捷删除按键处理回调函数
static void photo_key_down_callback(void);
// 模式切换按键处理回调函数
static void photo_mode_callback(void);

// UP按键处理回调函数
static void photo_up_callback(void);
static void photo_power_callback(void);
// ok按键处理回调函数
static void photo_ok_callback(void);

static void delayed_line_adjustment(lv_timer_t* timer);

//删除上下按键长按定时器
static void translation_delete_updown_timers(void);
//重读
static void reread_btn_event_cb(lv_event_t* e);
static void get_read_status_cb(lv_timer_t* timer);

/* 检查当前语言是否支持TTS朗读（仅支持中、英、日） */
static bool is_language_supported_for_tts(void)
{
    uint8_t lang = get_curr_language();
    return (lang == LANGUAGE_ZH_CN || lang == LANGUAGE_ZH_TW || lang == LANGUAGE_JA || lang == LANGUAGE_EN);
}

/* 安全删除控件函数 */
/**
 * @brief 安全删除LVGL对象
 *
 * 如果传入的对象指针非空且指向的对象有效，则删除该对象，并将指针置为NULL。
 *
 * @param obj_ptr 要删除的对象指针的地址
 */
static void translation_safe_delete_obj(lv_obj_t **obj_ptr)
{
    if(obj_ptr && *obj_ptr) {
        if(lv_obj_is_valid(*obj_ptr)) {
            // MLOG_DBG("Deleting object at %p\n", *obj_ptr);
            lv_obj_del(*obj_ptr);
        }
        *obj_ptr = NULL;
    }
}

/* 安全删除定时器 */
static void translation_safe_delete_timer(lv_timer_t **timer_ptr)
{
    if(timer_ptr && *timer_ptr) {
        // MLOG_DBG("Deleting timer at %p\n", *timer_ptr);
        lv_timer_del(*timer_ptr);
        *timer_ptr = NULL;
    }
}

/* 识别器清理函数（用于pthread_cleanup_push） */
static void recognizer_cleanup(void* arg)
{
    image_recognizer_t* rec = (image_recognizer_t*)arg;
    if (rec != NULL) {
        MLOG_DBG("清理: 销毁识别器 %p\n", rec);
        image_recognizer_destroy(rec);
    }
}

/* 线程本地变量：在独立线程中持有识别器 */
static __thread image_recognizer_t* g_thread_recognizer = NULL;

/* 图像识别线程函数
 * 在独立线程中执行图像识别，完成后通过lv_async_call通知UI更新
 */
static void* recognize_image_thread(void* arg)
{
    (void)arg;
    int ret = -1;
    int my_id; // 保存本次识别的ID，用于后续验证

    pthread_mutex_lock(&g_recognize_mutex);

    // 检查是否被取消
    if (g_recognize_cancelled) {
        MLOG_DBG("图像识别已被取消\n");
        g_recognize_running = false;
        g_recognize_completed = false;
        pthread_mutex_unlock(&g_recognize_mutex);
        return NULL;
    }

    // 保存本次识别的ID
    my_id = g_recognize_id;
    const char* image_path = g_recognize_image_path;
    pthread_mutex_unlock(&g_recognize_mutex);

    MLOG_DBG("开始图像识别线程: %s\n", image_path);

    char prompt[512] = { 0 };
    snprintf(prompt, sizeof(prompt), "%s", str_language_translate_text_in_image[get_curr_language()]);

    g_thread_recognizer = image_recognizer_create();
    if (g_thread_recognizer == NULL) {
        MLOG_ERR("创建识别器失败\n");
        ret = -1;
    } else {
        // 注册清理函数，确保线程被取消时销毁识别器
        pthread_cleanup_push(recognizer_cleanup, g_thread_recognizer);

        // 执行识别
        ret = image_recognizer_from_file(g_thread_recognizer, image_path, prompt, g_recognize_result, sizeof(g_recognize_result));

        // 正常退出：先取消注册(0=不执行清理，由我们手动销毁)
        pthread_cleanup_pop(0);
        image_recognizer_destroy(g_thread_recognizer);
        g_thread_recognizer = NULL;
    }

    pthread_mutex_lock(&g_recognize_mutex);

    // 检查是否被取消
    if (g_recognize_cancelled) {
        MLOG_DBG("图像识别已完成但已被取消\n");
        g_recognize_running = false;
        g_recognize_completed = false;
        pthread_mutex_unlock(&g_recognize_mutex);
        return NULL;
    }

    // 检查ID是否过期（如果ID变了，说明已有新的识别任务）
    if (my_id != g_recognize_id) {
        MLOG_DBG("图像识别已完成但ID已过期，忽略结果\n");
        g_recognize_running = false;
        pthread_mutex_unlock(&g_recognize_mutex);
        return NULL;
    }

    if (ret != 0) {
        MLOG_ERR("图像识别失败: %s\n", image_recognizer_get_error_string(ret));
        g_recognize_result[0] = '\0';
    } else {
        MLOG_DBG("图像识别成功\n");
    }

    g_recognize_running = false;
    g_recognize_completed = true; // 标记识别已完成
    pthread_mutex_unlock(&g_recognize_mutex);

    // 通过异步调用更新UI（确保在主线程中执行）
    lv_async_call(recognize_result_ready, NULL);

    return NULL;
}

/* 取消正在进行的图像识别 */
static void cancel_recognize(void)
{
    pthread_mutex_lock(&g_recognize_mutex);

    if (g_recognize_running) {
        MLOG_DBG("请求取消图像识别\n");
        g_recognize_cancelled = true;
        g_recognize_id++; // 递增ID，标记旧任务过期
        g_cancelled_result = true;
        pthread_t thread_to_cancel = g_recognize_thread;
        g_recognize_thread = 0;
        g_recognize_running = false;
        pthread_mutex_unlock(&g_recognize_mutex);

        // 设置取消标志后分离线程，不再等待
        // 分离后线程会自己清理资源（通过pthread_cleanup）
        pthread_detach(thread_to_cancel);

        g_recognize_cancelled = false;
        g_recognize_completed = false;
        g_recognize_result[0] = '\0';
        MLOG_DBG("图像识别已取消（线程已分离，ID: %d）\n", g_recognize_id);
    } else {
        pthread_mutex_unlock(&g_recognize_mutex);
    }
}

/* 开始图像识别（异步，不阻塞UI） */
static void start_recognize_async(const char* image_path)
{
    pthread_mutex_lock(&g_recognize_mutex);

    // 如果有正在运行的识别，先取消
    if (g_recognize_running) {
        g_recognize_cancelled = true;
        g_recognize_id++; // 递增ID，标记旧任务过期
        pthread_t old_thread = g_recognize_thread;
        g_recognize_thread = 0;
        g_recognize_running = false;
        pthread_mutex_unlock(&g_recognize_mutex);

        // 直接分离旧线程，不再等待
        pthread_detach(old_thread);
        MLOG_DBG("旧识别线程已分离\n");

        g_recognize_cancelled = false;
        g_recognize_completed = false;
    }

    // 保存图片路径
    strncpy(g_recognize_image_path, image_path, sizeof(g_recognize_image_path) - 1);
    g_recognize_image_path[sizeof(g_recognize_image_path) - 1] = '\0';

    // 重置结果
    g_recognize_result[0] = '\0';

    // 创建识别线程
    g_recognize_running = true;
    g_recognize_completed = false; // 重置完成标志
    g_recognize_id++; // 递增ID，标记新任务
    int this_id = g_recognize_id;

    pthread_mutex_unlock(&g_recognize_mutex);

    if (pthread_create(&g_recognize_thread, NULL, recognize_image_thread, NULL) != 0) {
        MLOG_ERR("创建识别线程失败\n");
        pthread_mutex_lock(&g_recognize_mutex);
        g_recognize_running = false;
        g_recognize_completed = false;
        pthread_mutex_unlock(&g_recognize_mutex);
        return;
    }

    MLOG_DBG("图像识别线程已启动 (ID: %d)\n", this_id);
}

/* 识别结果就绪回调（在线程完成后由主线程调用） */
static void recognize_result_ready(void* data)
{
    (void)data;

    // 检查识别是否已完成，并获取当前任务ID
    pthread_mutex_lock(&g_recognize_mutex);
    bool completed = g_recognize_completed;
    bool has_result = (g_recognize_result[0] != '\0');
    int current_id = g_recognize_id; // 获取当前任务ID
    pthread_mutex_unlock(&g_recognize_mutex);

    // 如果识别未完成（被取消或还未开始），直接返回
    if (!completed) {
        MLOG_DBG("识别未完成，忽略回调\n");
        return;
    }

    // 检查任务是否已过期（被后续任务替代）
    if (current_id != g_recognize_id) {
        MLOG_DBG("任务是过期任务，忽略回调 (old=%d, current=%d)\n", current_id, g_recognize_id);
        return;
    }

    // 安全检查：确保LVGL系统正常
    if (!lv_obj_is_valid(lv_scr_act())) {
        MLOG_DBG("屏幕对象无效，忽略识别结果\n");
        return;
    }

    // 安全检查：确保result_desc_label有效
    if (result_desc_label == NULL || !lv_obj_is_valid(result_desc_label)) {
        MLOG_DBG("结果标签已无效，忽略识别结果\n");
        return;
    }

    if (has_result) {
        pthread_mutex_lock(&g_recognize_mutex);
        result_text = g_recognize_result;
        pthread_mutex_unlock(&g_recognize_mutex);

        // 最终确认对象有效性后再操作
        if (!lv_obj_is_valid(result_desc_label)) {
            MLOG_DBG("识别成功但标签已无效\n");
            return;
        }

        /* 识别成功，更新显示内容 */
        lv_label_set_text(result_desc_label, result_text);

        // 再次检查避免布局更新时对象被删除
        if (!lv_obj_is_valid(result_desc_label)) {
            MLOG_DBG("布局更新前标签已无效\n");
            return;
        }

        /* 强制布局更新 */
        lv_obj_update_layout(result_desc_label);

        // 创建延迟定时器前再次检查
        if (!lv_obj_is_valid(result_desc_label)) {
            MLOG_DBG("布局更新后标签已无效\n");
            return;
        }

        /* 延迟获取高度，确保布局完成 */
        lv_timer_t* layout_timer = lv_timer_create(delayed_line_adjustment, 50, NULL);
        if (layout_timer != NULL) {
            lv_timer_set_repeat_count(layout_timer, 1); // 只执行一次
        }

        MLOG_DBG("Translation success - updated result\n");
        g_cancelled_result = false; // 识别成功后重置取消标记
    } else {
        /* 识别失败，显示失败信息 */
        // 根据取消标记显示不同提示
        if (g_cancelled_result) {
            // 取消导致的失败，提示重新拍照
            lv_label_set_text(result_desc_label, str_language_abnormal_exit_retake_photo[get_curr_language()]);
            MLOG_DBG("Translation cancelled - show retake photo message\n");
        } else {
            // 网络错误导致的失败
            lv_label_set_text(result_desc_label, str_language_translation_failed_please_check_network[get_curr_language()]);
            MLOG_DBG("Translation failed - show failure message\n");
        }
    }
}

static void ttp_start_cb(lv_timer_t* timer)
{
    char text[4096] = { 0 };
    strncpy(text, result_text, sizeof(text) - 1);
    ttp_play(text);
    if (get_play_switch() && is_language_supported_for_tts()) {
        lv_label_set_text(s_reread_label, str_language_pause_reading[get_curr_language()]);
        lv_obj_clear_flag(s_reread_btn, LV_OBJ_FLAG_HIDDEN);
        if (read_status_timer == NULL)
            read_status_timer = lv_timer_create(get_read_status_cb, 1000, NULL);
    }
    lv_timer_del(timer);
}

static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_GESTURE: {
            // 获取手势方向，需要 TP 驱动支持
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch(dir) {
                case LV_DIR_RIGHT: {
                    /* 取消注册拍照后处理回调函数 */
                    takephoto_unregister_callback();
                    /* 取消拍照按键处理回调函数 */
                    set_current_page_handler(NULL);
                    takephoto_unregister_menu_callback();
                    /* 安全删除所有资源 */
                    voice_arc_delete();
                    delete_viewfinder();
                    delete_zoom_bar();

                    MESSAGE_S Msg = {0};
                    // 通知mode关闭时要关闭sensor
                    Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
                    Msg.arg1      = 1;
                    MODEMNG_SendMessage(&Msg);
                    memset(&Msg, 0, sizeof(MESSAGE_S));
                    // 进入BOOT模式
                    Msg.topic     = EVENT_MODEMNG_MODESWITCH;
                    Msg.arg1      = WORK_MODE_BOOT;
                    MODEMNG_SendMessage(&Msg);
                    delete_zoombar_timer_handler();
                    takephoto_cancel_focus();
                    delete_wifi_img();
                    /* 清除播放线程 */
                    ttp_deinit();
                    takephoto_unregister_all_callback();
                    /* 返回主页 */
                    ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                        false, true);
                }
                default: break;
            }
            break;
        }
        default: break;
        }
}

static void release_shoot_translation_resources_cb(void)
{
    /* 取消注册拍照后处理回调函数 */
    takephoto_unregister_callback();
    /* 取消拍照按键处理回调函数 */
    set_current_page_handler(NULL);
    takephoto_unregister_menu_callback();
    /* 安全删除所有资源 */
    voice_arc_delete();
    delete_viewfinder();
    delete_zoom_bar();
    wifi_check_dialog_close();
    delete_zoombar_timer_handler();
    takephoto_cancel_focus();
    delete_wifi_img();
    takephoto_unregister_all_callback();
    /* 清除播放线程 */
    ttp_deinit();
}

/* 创建AI拍照页面 */
/**
 * @brief 创建AI拍照翻译界面
 *
 * 此函数用于创建AI拍照翻译功能的用户界面。
 *
 * @param ui 用户界面指针
 */
void create_ai_tranlation(lv_ui_t *ui)
{
    MLOG_DBG("[AI Camera] Creating AI camera screen\n");
    set_exit_completed(false);
    /* 如果页面已存在，先删除 */
    // translation_safe_delete_obj(&page_ai_shoot_translation_s);
    set_zoom_level(1);
    int32_t sensor_type = takephoto_get_current_sensor_type();
    if (sensor_type == TAKEPHOTO_SENSOR_SELFIE)
        disable_focus();

    /* 创建页面容器 */
    page_ai_shoot_translation_s = lv_obj_create(NULL);
    if (!page_ai_shoot_translation_s) {
        MLOG_ERR("[AI Camera] Failed to create screen container!\n");
        return;
    }

    lv_obj_remove_style_all(page_ai_shoot_translation_s);
    lv_obj_set_size(page_ai_shoot_translation_s, H_RES, V_RES);
    lv_obj_set_style_bg_color(page_ai_shoot_translation_s, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_ai_shoot_translation_s, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT); // 不透明度
    lv_obj_add_event_cb(page_ai_shoot_translation_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    /* 创建顶部标题栏 */
    lv_obj_t *header = lv_obj_create(page_ai_shoot_translation_s);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(0), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 创建返回按钮 */
    lv_obj_t* back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 100, 52);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x171717), 0);
    lv_obj_set_style_radius(back_btn, 20, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);
    lv_obj_set_style_text_font(back_label, &lv_font_SourceHanSerifSC_Regular_30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    /* 添加返回事件 */
    lv_obj_add_event_cb(back_btn, translation_back_cb, LV_EVENT_CLICKED, NULL);

    /* 创建标题 */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, str_language_photo_translate[get_curr_language()]);
    lv_obj_set_style_text_font(title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_center(title);
    lv_obj_set_width(title, 200);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    create_wifi_img(header, -190, 18);
    // 音色选择按钮（右侧）
    lv_obj_t* voice_style_btn = lv_btn_create(header);
    lv_obj_set_size(voice_style_btn, 100, 52);
    lv_obj_set_pos(voice_style_btn, H_RES - 104, 4); // 右侧位置
    lv_obj_set_style_pad_all(voice_style_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(voice_style_btn, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(voice_style_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(voice_style_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *voice_style_label = lv_label_create(voice_style_btn);
    lv_obj_set_style_text_align(voice_style_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(voice_style_label, volume_icon_get()); // 使用音量图标
    lv_obj_align(voice_style_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(voice_style_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 添加设置音量
    lv_obj_add_event_cb(voice_style_btn, voice_value_btn_event_cb, LV_EVENT_CLICKED, NULL);

    voice_man = lv_imagebutton_create(header);
    lv_obj_align(voice_man, LV_ALIGN_RIGHT_MID, -128, 0);
    lv_obj_set_size(voice_man, 48, 48);
    set_voice_style_icon(voice_man);
    if (get_currindex()) {
        show_image(voice_man, "女声.png");
    } else {
        show_image(voice_man, "男声.png");
    }
    lv_obj_set_style_pad_all(voice_man, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(voice_man, voice_style_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* 创建相机预览区域 */
    translation_preview_s = lv_img_create(page_ai_shoot_translation_s);
    lv_obj_set_size(translation_preview_s, H_RES, V_RES - 100);
    lv_obj_align(translation_preview_s, LV_ALIGN_TOP_MID, 0, 50);

    create_viewfinder(page_ai_shoot_translation_s);

    lv_obj_t *tips_label = lv_label_create(page_ai_shoot_translation_s);
    lv_obj_set_style_text_align(tips_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(tips_label, str_language_please_take_photo_of_text[get_curr_language()]);
    lv_obj_add_style(tips_label, &ttf_font_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(tips_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_color(tips_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    create_zoom_bar(page_ai_shoot_translation_s);
    /* 拍照按键处理回调函数 */
    set_current_page_handler(takephoto_key_handler);
    /* 注册拍照后处理回调函数 */
    takephoto_register_callback(translation_show_result);
    takephoto_register_menu_callback(translation_menu_callback);
    takephoto_register_zoomin_callback(zoomin_key_cb);
    takephoto_register_zoomout_callback(zoomout_key_cb);
    takephoto_register_down_callback(photo_key_down_callback);
    takephoto_register_up_callback(photo_up_callback);
    takephoto_register_mode_callback(photo_mode_callback);
    takephoto_register_long_menu_callback(photo_mode_callback);
    takephoto_register_ok_callback(photo_ok_callback);
    takephoto_register_power_callback(photo_power_callback);
    /* 检查WiFi连接状态 */
    wifi_check_and_show_dialog(page_ai_shoot_translation_s, wifi_return_to_translation, release_shoot_translation_resources_cb, ui);

    /* 创建ttp线程 */
    ttp_init();

    MLOG_DBG("[AI Camera] AI camera screen created successfully\n");
}

void translation_result_resources(void) //要先删除结果页面在加载拍照页面
{
    /* 取消正在进行的图像识别 */
    cancel_recognize();

    /* 清理结果页面相关资源 */
    translation_safe_delete_timer(&update_result_timer);
    translation_delete_updown_timers();
    result_desc_label = NULL;
    voice_arc_delete();
    delete_wifi_img();
    translation_safe_delete_timer(&read_status_timer);
    /* 安全删除结果页面 */
    lv_obj_t* current_scr = lv_scr_act();
    if (current_scr && lv_obj_is_valid(current_scr) && current_scr != page_ai_shoot_translation_s) {
        translation_safe_delete_obj(&current_scr);
    }
}

static void gesture_result_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_GESTURE: {
            // 获取手势方向，需要 TP 驱动支持
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch(dir) {
                case LV_DIR_RIGHT: {
                    /* tts+player复位 */
                    ttp_reset();
                    translation_result_resources();
                    /* 确保拍照页面存在 */
                    if(!page_ai_shoot_translation_s || !lv_obj_is_valid(page_ai_shoot_translation_s)) {
                        MLOG_WARN("Camera screen not valid, recreating...\n");
                        create_ai_tranlation(NULL);
                    } else {
                        /* 设置拍照按键处理回调函数 */
                        set_current_page_handler(takephoto_key_handler);
                    }
                    /* 切换回拍照页面 */
                    lv_scr_load(page_ai_shoot_translation_s);
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

/* 更新滚动信息 */
static void update_scroll_info(void)
{
    if (!text_cont || !lv_obj_is_valid(text_cont))
        return;

    current_scroll_pos = lv_obj_get_scroll_y(text_cont);

    lv_coord_t cont_height = lv_obj_get_height(result_desc_label); // 文本总高度
    lv_coord_t cont_height2 = lv_obj_get_height(text_cont); // 容器可视高度

    /* 计算最大滚动位置：文本总高度 - 容器可视高度 */
    max_scroll_pos = (cont_height > cont_height2) ? (cont_height - cont_height2 + 30) : 0;
}

void text_scroll_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCROLL) {
        update_scroll_info();
    }
}

/* 显示识别结果 */
static void translation_show_result(void)
{
    delete_zoombar_timer_handler();//删除自动隐藏缩放UI timer
    set_zoom_level(1);
    delete_viewfinder();
    hide_zoom_bar();
    delete_wifi_img();
    voice_arc_delete();
    /* 取消拍照按键处理回调函数 */
    set_current_page_handler(NULL);

    /* 创建结果页面容器 */
    lv_obj_t* result_page = lv_obj_create(NULL);
    if (!result_page) {
        MLOG_ERR("[AI Result] Failed to create result page!\n");
        return;
    }

    lv_obj_remove_style_all(result_page);
    lv_obj_set_size(result_page, H_RES, V_RES);
    lv_obj_set_style_bg_color(result_page, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(result_page, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(result_page, gesture_result_event_handler, LV_EVENT_GESTURE, NULL);

    /* 创建顶部标题栏 */
    lv_obj_t* header = lv_obj_create(result_page);
    lv_obj_set_size(header, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(header, lv_color_hex(0), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, 0);

    lv_obj_t* back_btn = lv_btn_create(header);
    if (back_btn) {
        lv_obj_set_size(back_btn, 100, 52);
        lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x171717), 0);
        lv_obj_set_style_radius(back_btn, 20, 0);
        lv_obj_set_style_pad_all(back_btn, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(back_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t* back_label = lv_label_create(back_btn);
        if (back_label) {
            lv_label_set_text(back_label, LV_SYMBOL_LEFT);
            // lv_obj_set_style_text_font(back_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 24), 0);
            lv_obj_center(back_label);
            lv_obj_set_style_text_font(back_label, &lv_font_SourceHanSerifSC_Regular_30,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        /* 添加返回事件 */
        lv_obj_add_event_cb(back_btn, translation_back_btn_result_cb, LV_EVENT_CLICKED, NULL);
    }

    /* 创建标题文本 */
    lv_obj_t* title_label = lv_label_create(header);
    if (title_label) {
        lv_label_set_text(title_label, str_language_translation_result[get_curr_language()]);
        lv_obj_set_style_text_font(title_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), 0);
        lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
        lv_obj_center(title_label);
    }

    // 在上方添加一条分割线
    lv_obj_t *up_line = lv_line_create(result_page);
    static lv_point_precise_t points_line[] = {{10, 60}, {640, 60}};
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);



    /* 创建主体内容容器 */
    lv_obj_t* content = lv_obj_create(result_page);
    lv_obj_set_size(content, LV_PCT(100), V_RES - 62);
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(0), 0);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(content, 0, 0);

    MLOG_DBG("show pic: %s\n", pic_filepath);
    char path_small[100] = {0};
    char path_large[100] = {0};
    get_thumbnail_path(pic_filepath, path_small, sizeof(path_small), PHOTO_SMALL_PATH);
    get_thumbnail_path(pic_filepath, path_large, sizeof(path_large), PHOTO_LARGE_PATH);

    /* 获取图片路径 */
    extern void resize_img_to_fit_screen(const char* path);
    const char* real_path = strchr(path_large, '/');
    MLOG_DBG("[AI Result] PATH : %s\n", real_path);
    resize_img_to_fit_screen(real_path);

    char* rel_large = strchr(path_large, '/');
    strncpy(pic_thumbnail, rel_large, sizeof(pic_thumbnail));
    /* 识别图片 */
    lv_obj_t* result_img = lv_img_create(content);
    if (result_img) {
        lv_obj_set_size(result_img, 300, 180);
        lv_obj_align(result_img, LV_ALIGN_LEFT_MID, 10, -40);
        lv_obj_set_style_border_width(result_img, 1, 0);
        lv_obj_set_style_border_color(result_img, lv_color_hex(0x4da6ff), 0);
        lv_obj_set_style_radius(result_img, 0, 0);
        lv_obj_set_style_pad_all(result_img, 0, 0);
        lv_img_set_src(result_img, "A:" RESIZE_IMG_PATH);
    }

    if (get_play_switch() && is_language_supported_for_tts()) {
        s_reread_btn = lv_btn_create(content);
        lv_obj_set_size(s_reread_btn, 160, 60);
        lv_obj_align_to(s_reread_btn, result_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(s_reread_btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(s_reread_btn, 20, 0);
        lv_obj_set_style_shadow_width(s_reread_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_reread_btn, LV_OBJ_FLAG_HIDDEN);
        /* 添加重读事件回调 */
        lv_obj_add_event_cb(s_reread_btn, reread_btn_event_cb, LV_EVENT_CLICKED, NULL);

        s_reread_label = lv_label_create(s_reread_btn);
        lv_obj_center(s_reread_label);
        lv_obj_set_width(s_reread_label, 160);
        lv_obj_add_style(s_reread_label, &ttf_font_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_reread_label, lv_color_white(), 0);
        lv_label_set_long_mode(s_reread_label, LV_LABEL_LONG_SCROLL);
    }
    /* 右侧文本容器 - 现代化信纸风格 */
    text_cont = lv_obj_create(content);
    lv_obj_set_size(text_cont, 300, LV_PCT(90));
    lv_obj_align(text_cont, LV_ALIGN_RIGHT_MID, -10, 0);

    // 信纸背景样式
    lv_obj_set_scrollbar_mode(text_cont, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(text_cont, LV_DIR_VER);
    lv_obj_set_style_bg_color(text_cont, lv_color_hex(0xF8F5E6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(text_cont, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(text_cont, lv_color_hex(0xD3C9A1), LV_PART_MAIN);
    lv_obj_set_style_border_width(text_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(text_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(text_cont, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(text_cont, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(text_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(text_cont, 2, LV_PART_MAIN); // 修正：使用正确的函数名
    lv_obj_set_style_pad_all(text_cont, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(text_cont, text_scroll_event_cb ,LV_EVENT_SCROLL, NULL);

    /* 识别结果描述 - 现代化信纸文字 */
    result_desc_label = lv_label_create(text_cont);
    if(result_desc_label) {
        lv_label_set_text(result_desc_label, str_language_recognizing[get_curr_language()]);
        lv_obj_set_width(result_desc_label, 270);
        lv_obj_align(result_desc_label, LV_ALIGN_TOP_LEFT, 0, 0);

        // 信纸文字样式
        lv_obj_set_style_text_font(result_desc_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 22), 0);
        const lv_font_t *font1 = lv_obj_get_style_text_font(result_desc_label, LV_PART_MAIN);
        lv_coord_t line_height1 = lv_font_get_line_height(font1);
        lv_obj_set_style_text_color(result_desc_label, lv_color_hex(0x5C4B37), 0);
        lv_obj_set_style_text_align(result_desc_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_line_space(result_desc_label, line_height1, 0); // 设置行高与线条间距一致

        // 确保文本在线条上方
        lv_obj_move_foreground(result_desc_label);
    }
    // 添加信纸线条 - 创建足够多的线条覆盖整个容器
    const lv_font_t *font = lv_obj_get_style_text_font(result_desc_label, LV_PART_MAIN);
    lv_coord_t line_height = lv_font_get_line_height(font);
    int container_height = lv_obj_get_height(text_cont);
    if(container_height == 0) container_height = 300; // 默认高度
    int num_lines = container_height / line_height + 2; // 多创建一条确保覆盖

    for(int i = 0; i < num_lines; i++) {
        lv_obj_t *line = lv_line_create(text_cont);
        /* 设置线的点：从(0,0)到(270,0) */
        static lv_point_precise_t points[] = {{0, 0}, {270, 0}};
        lv_line_set_points(line, points, 2);
        lv_obj_set_style_line_color(line, lv_color_hex(0xE8E0C7), LV_PART_MAIN);
        lv_obj_set_style_line_width(line, 1, LV_PART_MAIN);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, i * line_height);
    }

    // 添加信纸角装饰
    corner_deco = lv_img_create(text_cont);
    show_image(corner_deco, "羽毛笔.png");
    lv_obj_set_size(corner_deco, 48, 48);
    lv_obj_align(corner_deco, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    lv_obj_set_style_img_opa(corner_deco, LV_OPA_50, LV_PART_MAIN);

    // 音色选择按钮（右侧）
    lv_obj_t* voice_style_btn = lv_btn_create(header);
    lv_obj_set_size(voice_style_btn, 100, 52);
    lv_obj_set_pos(voice_style_btn, H_RES - 104, 4); // 右侧位置
    lv_obj_set_style_pad_all(voice_style_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(voice_style_btn, lv_color_hex(0x171717), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(voice_style_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(voice_style_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *voice_style_label = lv_label_create(voice_style_btn);
    lv_obj_set_style_text_align(voice_style_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(voice_style_label, volume_icon_get()); // 使用音量图标
    lv_obj_align(voice_style_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(voice_style_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 添加设置音量
    lv_obj_add_event_cb(voice_style_btn, voice_value_btn_event_cb, LV_EVENT_CLICKED, NULL);
    create_wifi_img(header, -140, 18);

    /* 加载结果页面 */
    lv_scr_load(result_page);

    /* 启动翻译结果更新定时器 */
    translation_safe_delete_timer(&update_result_timer);
    update_result_timer = lv_timer_create(translation_update_result_cb, 500, NULL);
    //注册结果页面的按键事件
    set_current_page_handler(translation_result_key_handler);

    if (page_ai_shoot_translation_s != NULL && lv_obj_is_valid(page_ai_shoot_translation_s)) {
        MLOG_DBG("删除拍照界面\n");
        lv_obj_del(page_ai_shoot_translation_s);
        page_ai_shoot_translation_s = NULL;
    }
}

/* 延迟线条调整函数 */
static void delayed_line_adjustment(lv_timer_t* timer)
{
    if (!result_desc_label || !lv_obj_is_valid(result_desc_label) || !lv_obj_is_valid(text_cont)) {
        lv_timer_del(timer);
        return;
    }

    /* 获取字体和高度信息 */
    const lv_font_t *font = lv_obj_get_style_text_font(result_desc_label, LV_PART_MAIN);
    lv_coord_t height = lv_obj_get_height(result_desc_label);
    lv_coord_t line_height = lv_font_get_line_height(font);

    MLOG_DBG("Delayed adjustment - height:%d line_height:%d height/line_height:%d\n",
             height, line_height, height/line_height);

    /* 如果高度超过300，重新设置羽毛笔图标的位置 */
    if(height > 300) {
        // 确保羽毛笔图标存在
        if(corner_deco && lv_obj_is_valid(corner_deco)) {
            // 获取文本容器的尺寸
            lv_coord_t container_width = lv_obj_get_width(text_cont);
            lv_coord_t container_height = lv_obj_get_height(text_cont);

            // 获取羽毛笔图标的尺寸
            lv_coord_t icon_width = lv_obj_get_width(corner_deco);
            lv_coord_t icon_height = lv_obj_get_height(corner_deco);

            // 计算右下角位置（考虑边距）
            lv_coord_t target_x = container_width - icon_width - 5; // 右边距5px
            lv_coord_t target_y = container_height - icon_height - 5; // 底边距5px

            // 设置羽毛笔图标的位置
            lv_obj_set_pos(corner_deco, 0, target_y+50);
            // lv_obj_align(corner_deco, LV_ALIGN_BOTTOM_RIGHT, -5, -5);

            MLOG_DBG("Feather icon positioned at (%d, %d), container size: %dx%d\n",
                     target_x, target_y, container_width, container_height);
        }
    }

    /* 创建新的线条 */
    if(line_height > 0) {
        int line_count = (height + line_height - 1) / line_height; // 向上取整
        MLOG_DBG("Line count: %d\n", line_count);

        for(int i = 0; i < line_count; i++) {
            lv_obj_t *line = lv_line_create(text_cont);
            /* 设置线的点：从(0,0)到(270,0) */
            static lv_point_precise_t points[] = {{0, 0}, {270, 0}};
            lv_line_set_points(line, points, 2);
            lv_obj_set_style_line_color(line, lv_color_hex(0xE8E0C7), LV_PART_MAIN);
            lv_obj_set_style_line_width(line, 1, LV_PART_MAIN);
            lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, i * line_height);
            MLOG_DBG("Created line at y=%d\n", i * line_height);
        }
    }
    update_scroll_info();
    /* 删除定时器 */
    lv_timer_del(timer);
    if (get_play_switch() && is_language_supported_for_tts()) {
        /* 通知ttp播放 */
        lv_timer_t* tts_timer = lv_timer_create(ttp_start_cb, 5, NULL);
        lv_timer_set_repeat_count(tts_timer, 1); // 只执行一次
    }
}

/* 翻译结果更新定时器回调 - 启动异步识别 */
static void translation_update_result_cb(lv_timer_t* timer)
{
    (void)timer;
    /* 删除定时器 */
    translation_safe_delete_timer(&update_result_timer);

    // 启动异步识别（不阻塞UI）
    start_recognize_async(pic_thumbnail);
}

/* 返回按钮回调函数 */
static void translation_back_btn_result_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        /* tts+player复位 */
        ttp_reset();
        translation_result_resources();
        /* 确保拍照页面存在 */
        if (!page_ai_shoot_translation_s || !lv_obj_is_valid(page_ai_shoot_translation_s)) {
            MLOG_WARN("Camera screen not valid, recreating...\n");
            create_ai_tranlation(NULL);
        } else {
            /* 设置拍照按键处理回调函数 */
            set_current_page_handler(takephoto_key_handler);
        }
        /* 切换回拍照页面 */
        lv_scr_load(page_ai_shoot_translation_s);
    }
}

/* 向上滚动 */
static void scroll_up(void)
{
    if (!text_cont || !lv_obj_is_valid(text_cont))
        return;

    int new_pos = current_scroll_pos - 50; // 每次滚动30像素
    if (new_pos < 0)
        new_pos = 0;

    lv_obj_scroll_to_y(text_cont, new_pos, LV_ANIM_ON);
    current_scroll_pos = new_pos;
}

/* 向下滚动 */
static void scroll_down(void)
{
    if (!text_cont || !lv_obj_is_valid(text_cont))
        return;

    int new_pos = current_scroll_pos + 50; // 每次滚动30像素

    // 确保不超过最大滚动位置
    if (new_pos > max_scroll_pos) {
        new_pos = max_scroll_pos;
        MLOG_DBG("已滚动到底部\n");
    }

    lv_obj_scroll_to_y(text_cont, new_pos, LV_ANIM_ON);
    current_scroll_pos = new_pos;
}

// 长按UP按键检测定时器回调函数
static void up_long_press_timer_cb(lv_timer_t *t)
{
    // 定时器触发说明按键已经持续按下300ms，执行长按逻辑
    up_long_press_flag = true;
    // 100ms周期继续检测, 直到按键松开
    lv_timer_set_period(t, 100);
    lv_timer_reset(t);

    // 执行向上滚动
    scroll_up();
}

// 长按DOWN按键检测定时器回调函数
static void down_long_press_timer_cb(lv_timer_t *t)
{
    // 定时器触发说明按键已经持续按下300ms，执行长按逻辑
    down_long_press_flag = true;
    // 100ms周期继续检测, 直到按键松开
    lv_timer_set_period(t, 100);
    lv_timer_reset(t);

    // 执行向下滚动
    scroll_down();
}

// UP按键处理函数
int32_t do_up(int32_t key_value)
{
    if(key_value == 1) {
        // 创建长按检测定时器, 300ms认为是长按
        up_long_press_flag = false;
        up_long_press_timer = lv_timer_create(up_long_press_timer_cb, 300, NULL);
    } else {
        // UP按键释放
        // 短按或者长按，在按键释放的时候都要删除定时器
        if(up_long_press_timer != NULL) {
            lv_timer_del(up_long_press_timer);
            up_long_press_timer = NULL;
        }
        // 短按触发一次向上滚动
        if(!up_long_press_flag) {
            MLOG_DBG("UP按键短按, 执行短按逻辑\n");
            scroll_up();
        }
        up_long_press_flag = false;
    }
    return 0;
}

// DOWN按键处理函数
int32_t do_down(int32_t key_value)
{
    if(key_value == 1) {
        // 创建长按检测定时器, 300ms认为是长按
        down_long_press_flag = false;
        down_long_press_timer = lv_timer_create(down_long_press_timer_cb, 300, NULL);
    } else {
        // DOWN按键释放
        // 短按或者长按，在按键释放的时候都要删除定时器
        if(down_long_press_timer != NULL) {
            lv_timer_del(down_long_press_timer);
            down_long_press_timer = NULL;
        }
        // 短按触发一次向下滚动
        if(!down_long_press_flag) {
            MLOG_DBG("DOWN按键短按, 执行短按逻辑\n");
            scroll_down();
        }
        down_long_press_flag = false;
    }
    return 0;
}

// 安全删除UP/DOWN长按定时器函数
static void translation_delete_updown_timers(void)
{
    if(up_long_press_timer != NULL) {
        lv_timer_del(up_long_press_timer);
        up_long_press_timer = NULL;
    }
    if(down_long_press_timer != NULL) {
        lv_timer_del(down_long_press_timer);
        down_long_press_timer = NULL;
    }
    up_long_press_flag = false;
    down_long_press_flag = false;
}

/* 返回按钮回调 */
static void translation_back_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        /* 关闭WiFi提示窗口 */
        wifi_check_dialog_close();
        delete_viewfinder();
        delete_zoom_bar();

        /* 取消注册拍照后处理回调函数 */
        takephoto_unregister_callback();
        /* 取消拍照按键处理回调函数 */
        set_current_page_handler(NULL);

        MESSAGE_S Msg = {0};
        // 通知mode关闭时要关闭sensor
        Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
        Msg.arg1      = 1;
        MODEMNG_SendMessage(&Msg);
        memset(&Msg, 0, sizeof(MESSAGE_S));
        // 进入BOOT模式
        Msg.topic     = EVENT_MODEMNG_MODESWITCH;
        Msg.arg1      = WORK_MODE_BOOT;
        MODEMNG_SendMessage(&Msg);
        voice_arc_delete();
        delete_zoombar_timer_handler();
        takephoto_cancel_focus();
        delete_wifi_img();
        /* 清除播放线程 */
        ttp_deinit();
        takephoto_unregister_all_callback();
        /* 返回主页 */
        ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1,
            LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
    }
}

// 菜单按键处理回调函数
static void photo_mode_callback(void)
{
    /* 取消注册拍照后处理回调函数 */
    takephoto_unregister_callback();
    /* 取消拍照按键处理回调函数 */
    set_current_page_handler(NULL);
    takephoto_unregister_menu_callback();

    MESSAGE_S Msg = {0};
    // 通知mode关闭时要关闭sensor
    Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
    Msg.arg1      = 1;
    MODEMNG_SendMessage(&Msg);
    memset(&Msg, 0, sizeof(MESSAGE_S));
    // 进入BOOT模式
    Msg.topic     = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1      = WORK_MODE_BOOT;
    MODEMNG_SendMessage(&Msg);
    voice_arc_delete();
    delete_viewfinder();
    delete_zoom_bar();

    delete_zoombar_timer_handler();
    takephoto_cancel_focus();
    delete_wifi_img();
    /* 清除播放线程 */
    ttp_deinit();
    takephoto_unregister_all_callback();
    /* 返回主页 */
    ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

// 放大按键事件处理
static void zoomin_key_cb(void)
{
    if(get_arc_handel() != NULL) {
        voice_arc_delete();
        voice_setting_arc_create();
        volume_add();
    } else {
        uint32_t new_level = get_zoom_level();;
        // 设置放大比例
        MESSAGE_S Msg = {0};
        Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
        Msg.arg1      = 0;
        snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
        MODEMNG_SendMessage(&Msg);
        // 更新UI
        update_zoom_bar(new_level);
    }
}

// 缩小按键事件处理
static void zoomout_key_cb(void)
{
    if(get_arc_handel() != NULL) {
        voice_arc_delete();
        voice_setting_arc_create();
        volume_reduce();
    } else {
        uint32_t new_level = get_zoom_level();
        // 设置放大比例
        MESSAGE_S Msg = {0};
        Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
        Msg.arg1      = 0;
        snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
        MODEMNG_SendMessage(&Msg);
        // 更新UI
        update_zoom_bar(new_level);
    }
}

static void translation_result_key_handler(int key_code, int key_value)
{
    switch(key_code) {
        case KEY_CAMERA:
        case KEY_MENU:
            if(!key_value) return;
            /* tts+player复位 */
            ttp_reset();
            translation_result_resources();
            /* 确保拍照页面存在 */
            if(!page_ai_shoot_translation_s || !lv_obj_is_valid(page_ai_shoot_translation_s)) {
                MLOG_WARN("Camera screen not valid, recreating...\n");
                create_ai_tranlation(NULL);
            } else {
                /* 设置拍照按键处理回调函数 */
                set_current_page_handler(takephoto_key_handler);
            }
            /* 切换回拍照页面 */
            lv_scr_load(page_ai_shoot_translation_s);
            break;
        case KEY_ZOOMIN: { // in
            do_zoomin(key_value);
        }; break;
        case KEY_ZOOMOUT: { // out
            do_zoomout(key_value);
        }; break;
        case KEY_UP: {
            // 向上滚动
            do_up(key_value);
        } break;
        case KEY_DOWN: {
            // 向下滚动
            do_down(key_value);
        } break;
        case KEY_OK: {
            if (get_play_switch() && is_language_supported_for_tts()) {
                lv_obj_send_event(s_reread_btn, LV_EVENT_CLICKED, NULL);
            }
        }; break;
        }
}

// 音色选择按钮事件回调
static void voice_value_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // 创建音量设置
        if(get_arc_handel() == NULL) {
            hide_zoom_bar();
            voice_setting_arc_create();
        } else {
            voice_arc_delete();
        }
    }
}

static void wifi_return_to_translation(void *user_data)
{
    lv_ui_t *ui = (lv_ui_t *)user_data;
    if(ui == NULL) {
        MLOG_ERR("UI is NULL, using default UI...\n");
        ui = &g_ui;
    }
    ui_load_scr_animation(ui, &page_ai_shoot_translation_s, 1, NULL, create_ai_tranlation, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}


static void voice_style_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    switch(code) {
        case LV_EVENT_CLICKED: {
            // 创建音色选择模态框
            create_voice_style_modal();
        }; break;
        default: break;
        }
}

//快捷删除按键处理回调函数
static void photo_key_down_callback(void)
{
    uint8_t selected_index = get_currindex();
    selected_index = (selected_index < 1) ? 1 : 0;
    set_checkbox_index(selected_index);
}

// 模式切换按键处理回调函数
static void translation_menu_callback(void)
{
    voice_style_management();
}

// UP按键处理回调函数
static void photo_up_callback(void)
{
    if (get_voicestyle_is_valid()) {
        uint8_t selected_index = get_currindex();
        selected_index = (selected_index > 0) ? 0 : 1;
        set_checkbox_index(selected_index);
    } else {
        static uint8_t currindex = 0;
        currindex = photo_getFlash_Index();
        /* 当前闪光灯不能控制 */
        if (!check_battery_for_flash_led(page_ai_shoot_translation_s)) {
            return;
        }
        currindex++;
        if (currindex > 2) {
            currindex = 0;
        }
        photo_setFlash_Index(currindex);
        MESSAGE_S Msg = { 0 };
        Msg.topic = EVENT_MODEMNG_SETTING;
        Msg.arg1 = PARAM_MENU_FLASH_LED;
        Msg.arg2 = currindex;
        MODEMNG_SendMessage(&Msg);
    }
}

static void photo_sesor_switch_completed_callback(void)
{
    enable_touch_events(); // 连拍结束，恢复触摸
    enable_hardware_input_device(0);
    enable_hardware_input_device(1);
    int32_t sensor_type = takephoto_get_current_sensor_type();
    if (sensor_type == TAKEPHOTO_SENSOR_SELFIE) {
        disable_focus();
    } else {
        enable_focus();
    }
}

// ok按键处理回调函数
static void photo_ok_callback(void)
{
    if (get_voicestyle_is_valid()) {
        make_sure_ok();
    } else {
    }
}

static void photo_power_callback(void)
{
    MLOG_DBG("ok按键, 切换前后摄像头\n");
    MESSAGE_S Msg = { 0 };
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1 = PARAM_MENU_SENSOR_SWITCH;
    MODEMNG_SendMessage(&Msg);

    disable_touch_events(); // 延时倒计时，禁用触摸
    disable_hardware_input_device(0);
    disable_hardware_input_device(1);

    completed_register_cb(photo_sesor_switch_completed_callback);
}

/* 重读按钮回调函数 */
static void reread_btn_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    ttp_status_s ttp_status = { 0 };
    if (code == LV_EVENT_CLICKED) {
        /* 检查是否有识别结果文本 */
        static uint32_t last_reread_click_time = 0;
        if (result_text != NULL && strlen(result_text) > 0) {
            // 获取当前时间
            uint32_t current_time = lv_tick_get();
            // 检查是否在点击间隔内
            if (current_time - last_reread_click_time < 1000) { //每1000ms内不允许再次点击
                MLOG_WARN("点击过于频繁，忽略此次点击\n");
                return;
            }
            // 更新上次点击时间
            last_reread_click_time = current_time;

            ttp_get_status(&ttp_status);
            /* 重新播放识别结果 */
            if (s_is_read) { // 暂停朗读
                ttp_reset();
                s_is_read = false;
                lv_label_set_text(s_reread_label, str_language_resume_reading[get_curr_language()]);
            } else { // 开始朗读
                if (ttp_status.tts_status == TTP_STATUS_TTS_IDLE && ttp_status.player_status == TTP_STATUS_PLAYER_IDLE) {
                    ttp_play(result_text);
                    s_is_read = true;
                    lv_label_set_text(s_reread_label, str_language_pause_reading[get_curr_language()]);
                } else {
                    MLOG_WARN("wait ttp idle:%d %d\n", ttp_status.tts_status, ttp_status.player_status);
                }
            }
            if (read_status_timer != NULL) {
                lv_timer_reset(read_status_timer);
            }
        } else {
            MLOG_WARN("No result text available for rereading\n");
        }
    }
}

static void get_read_status_cb(lv_timer_t* timer)
{
    ttp_status_s ttp_status = { 0 };
    ttp_get_status(&ttp_status);
    if (ttp_status.player_status == TTP_STATUS_PLAYER_IDLE && ttp_status.tts_status == TTP_STATUS_TTS_IDLE) {
        s_is_read = false;
        lv_label_set_text(s_reread_label, str_language_resume_reading[get_curr_language()]);
    }
}