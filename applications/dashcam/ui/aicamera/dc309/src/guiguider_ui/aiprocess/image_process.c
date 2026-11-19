/*
 * 进入拍照页面，且设置了AI模式的时候
 *
 * 使用状态接口，来控制线程的运行。
 */
// #############################################################################
// ! #region 1. 头文件与宏定义
// #############################################################################
#define DEBUG

#include "image_process.h"
#include "common/extract_thumbnail.h"
#include "config.h"
#include "face_smoothing/skin_smoother.h"
#include "img2img/img2img.h"
#include "jpegp.h"
#include "mlog.h"
#include "page_all.h"
#include "ui_common.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* AI 输出的图片大小需要与屏幕大小比例一致 */
#define AI_OUT_IMG_WIDTH 960
#define AI_OUT_IMG_HEIGHT 720
#define SUBPIC_WIDTH 640
#define SUBPIC_HEIGHT 480
#define THUMBNAIL_WIDTH 200
#define THUMBNAIL_HEIGHT 140

// #endregion
// #############################################################################
// ! #region 2. 数据结构定义
// #############################################################################

// #endregion
// #############################################################################
// ! #region 3. 全局变量 &  函数声明
// #############################################################################
static pthread_mutex_t processor_mutex = PTHREAD_MUTEX_INITIALIZER; // 任务锁

static bool is_thread_start = AI_PROCESS_IDLE; // 是否让任务开始执行
static char g_input_img_path[256] = { 0 }; // 输入图片路径，真实路径，不带 A:
static char g_output_img_path[256] = { 0 }; // 输出图片路径，真实路径，不带 A:
static char g_output_thumb_path[256] = { 0 }; // 输出缩略图路径，带 A:
static char g_style_prompt[1024] = { 0 }; // 提示词
static int g_api_retval = DEFALT_RETVAL; // AI处理返回值

static img2img_processor_t* img2imgprocessor = NULL;
static bool img2imgprocessor_destroyed = false; // 标记是否已销毁，防止双重释放
// #endregion
// #############################################################################
// ! #region 4. 内部工具函数（注意用static修饰）
// #############################################################################

/**
 * @brief 基于输入图片路径，生成 AI 处理后的输出路径
 *
 * 生成规则：
 * - 输入 DCIMxxxx.jpg → 输出 DCIMxxxx_AI0001.jpg
 * - 输入 DCIMxxxx_AI0020.jpg → 输出 DCIMxxxx_AI0021.jpg
 *
 * 通过 access() 检测文件是否存在，如存在则序号递增
 * 结果存储到变量 output_path
 */
static void generate_output_path(char* input_path, char* output_path, size_t path_size)
{
    char* filename = NULL; // 不带路径的文件名部分
    char base_name[256] = { 0 }; // 不带扩展名的基础名
    char pure_name[256] = { 0 }; // 用于生成输出名的基础部分
    char output_name[256] = { 0 };
    const char* dot = NULL;
    const char* path_prefix = NULL;

    // 1. 提取不带路径的文件名
    filename = get_basename(input_path);
    if (!filename) {
        MLOG_ERR("Failed to get filename from: %s\n", input_path);
        return;
    }

    // 2. 查找文件扩展名
    dot = strrchr(filename, '.');
    if (!dot) {
        MLOG_ERR("No file extension found in: %s\n", filename);
        return;
    }

    // 3. 提取基础文件名（不带扩展名）
    snprintf(base_name, sizeof(base_name), "%.*s", (int)(dot - filename), filename);
    MLOG_DBG("Base filename: %s\n", base_name);

    // 4. 检查是否已有 _AI 序号后缀
    int start_num = 1;
    int ai_num = 0;
    // 检查是否以 _AI 后面跟4位数字结尾
    if (sscanf(base_name, "%*[^_]_AI%4d", &ai_num) == 1) {
        // 匹配成功，提取基础名（去掉 _AI 后缀）
        snprintf(pure_name, sizeof(pure_name), "%.*s", (int)(strlen(base_name) - 7), base_name);
        start_num = ai_num + 1; // 从当前序号+1开始
    } else {
        // 没有 _AI 后缀，从 0001 开始
        snprintf(pure_name, sizeof(pure_name), "%s", base_name);
    }

    // 5. 查找下一个可用的序号
    int next_num = start_num;
    while (1) {
        snprintf(output_name, sizeof(output_name), "%s_AI%04d%s", pure_name, next_num, dot);
        path_prefix = PHOTO_ALBUM_IMAGE_PATH;
        snprintf(output_path, path_size, "%s%s", path_prefix, output_name);
        fix_path_validity(output_path);
        if (access(output_path, F_OK) != 0) {
            break; // 文件不存在，可以使用此序号
        }
        next_num++;
        if (next_num > 9999) {
            MLOG_ERR("No available filename number\n");
            output_path[0] = '\0';
            return;
        }
    }

    MLOG_INFO("Generated aiprocess output path: %s, input filename: %s\n", output_path, filename);
}

/*
 * @brief 给图片增加缩略图，修改原文件
 */
static void result_thumbnail_save(char* img_path)
{
    int ret;

    if (img_path == NULL || img_path[0] == '\0') {
        MLOG_ERR("图片路径为空，无法保存缩略图\n");
        return;
    }

    if (access(img_path, F_OK) != 0) {
        MLOG_ERR("图片不存在: %s\n", img_path);
        return;
    }

    // 生成缩略图
    JPEGP_PACKET_FILE_PARAM_S stParam = { 0 };
    stParam.src_file_name = (CVI_VOID*)img_path;
    stParam.dst_file_name = (CVI_VOID*)img_path;
    stParam.src_width = AI_OUT_IMG_WIDTH;
    stParam.src_height = AI_OUT_IMG_HEIGHT;
    stParam.subpic_width = SUBPIC_WIDTH;
    stParam.subpic_height = SUBPIC_HEIGHT;
    stParam.thumbnail_width = THUMBNAIL_WIDTH;
    stParam.thumbnail_height = THUMBNAIL_HEIGHT;
    ret = JPEGP_Gen_Thumbnail_And_SubPic_To_File(&stParam);
    if (ret == 0) {
        MLOG_DBG("保存缩略图成功\n");
        FILEMNG_AddFile(0, img_path);
        get_thumbnail_path(img_path, g_output_thumb_path, sizeof(g_output_thumb_path), PHOTO_LARGE_PATH);
    } else {
        MLOG_ERR("保存缩略图失败: %d\n", ret);
    }
}

// #endregion
// #############################################################################
// ! #region 5. 对外接口函数
// #############################################################################

void aiprocess_set_prompt(const char* word)
{
    snprintf(g_style_prompt, sizeof(g_style_prompt), "%s", word);
}

void aiprocess_clean_cache(void)
{
    memset(g_output_img_path, 0, sizeof(g_output_img_path));
    g_api_retval = DEFALT_RETVAL;
}

/* 取消当前AI处理并重置状态 */
void ai_process_cancel_and_reset(void)
{
    pthread_mutex_lock(&processor_mutex);
    is_thread_start = AI_PROCESS_IDLE;
    // 清除可能已生成的部分文件
    if (strlen(g_output_img_path) > 0 && access(g_output_img_path, F_OK) == 0) {
        remove(g_output_img_path);
        MLOG_DBG("删除已生成的部分文件: %s\n", g_output_img_path);
    }
    memset(g_output_img_path, 0, sizeof(g_output_img_path));
    memset(g_output_thumb_path, 0, sizeof(g_output_thumb_path)); // 同时清空缩略图路径
    g_api_retval = DEFALT_RETVAL;
    if (img2imgprocessor) {
        img2img_cancel(img2imgprocessor);
        // 标记需要在销毁回调中处理，由线程负责销毁，避免双重释放
        img2img_destroy(img2imgprocessor);
        img2imgprocessor_destroyed = true;
        img2imgprocessor = NULL; // 置空，防止重复cancel
    }
    pthread_mutex_unlock(&processor_mutex);
    MLOG_DBG("AI process cancelled and reset\n");
}

/* 获取处理结果真实路径 */
char* process_result_get(void)
{
    return g_output_img_path;
}

/* 获取处理结果缩略图路径，带 A: 前缀 */
char* process_result_get_thumbnail(void)
{
    return g_output_thumb_path;
}

int get_retval(void)
{
    return g_api_retval;
}

void set_defalt_retval(void)
{
    g_api_retval = DEFALT_RETVAL;
}

/* AI处理状态设置函数 */
void ai_process_state_set(bool state)
{
    pthread_mutex_lock(&processor_mutex);
    is_thread_start = state;
    pthread_mutex_unlock(&processor_mutex);
    MLOG_DBG("AI process state set to %d", state);
}

// #endregion
// #############################################################################
// ! #region 6. 线程处理函数
// #############################################################################

/* AI处理线程函数 */
void* thread_ai_process_main(void* arg)
{
    (void)arg;
    int ret;
    const char* src_path = NULL;

    while (1) {
        usleep(50000); // 50ms检查一次状态，更快响应
        pthread_mutex_lock(&processor_mutex);
        if (is_thread_start == AI_PROCESS_START) {
            ret = DEFALT_RETVAL;
            MLOG_DBG("开始AI图片处理类型: %d\n", AIModeSelect_GetMode());

            // 通过外部变量 is_album_pic 判断图片来源
            src_path = is_album_pic ? get_curr_pic_path() : get_ai_process_result_img_data(0);
            if (src_path == NULL || src_path[0] == '\0') {
                MLOG_ERR("AI图片处理输入路径为空\n");
                ret = -1000;
                pthread_mutex_unlock(&processor_mutex);
                goto AI_PROCESS_FINISH;
            }
            snprintf(g_input_img_path, sizeof(g_input_img_path), "%s", src_path);
            fix_path_validity(g_input_img_path); // 修正路径有效性
            generate_output_path(g_input_img_path, g_output_img_path, 256); // 生成输出路径
            if (g_output_img_path[0] == '\0') {
                MLOG_ERR("AI图片处理输出路径生成失败\n");
                ret = -1001;
                pthread_mutex_unlock(&processor_mutex);
                goto AI_PROCESS_FINISH;
            }
            pthread_mutex_unlock(&processor_mutex); // 尽快释放锁，允许取消

            if (AIModeSelect_GetMode() == AI_BEAUTY) { // 美颜处理
                skin_smoother_processor_t* skin_processor = skin_smoother_create();
                if (skin_processor) {
                    ret = skin_smoother_process_file(skin_processor, g_input_img_path, g_output_img_path);
                    if (ret != 0) {
                        MLOG_DBG("美颜处理失败: %d\n", ret);
                    } else {
                        MLOG_DBG("美颜处理成功！\n");
                    }
                    skin_smoother_destroy(skin_processor);
                } else {
                    MLOG_ERR("创建美颜处理器失败\n");
                    ret = -1002;
                }
            } else { // 场景/背景/年龄切换处理
                img2imgprocessor = img2img_create();
                img2imgprocessor_destroyed = false; // 重置销毁标志
                if (img2imgprocessor) {
                    img2img_params_t params = img2img_default_params();
                    params.prompt = g_style_prompt;
                    params.width = AI_OUT_IMG_WIDTH;
                    params.height = AI_OUT_IMG_HEIGHT;
                    ret = img2img_process_file(img2imgprocessor, g_input_img_path, &params, g_output_img_path);
                    if (ret != 0) {
                        MLOG_DBG("图像处理失败: %s\n", img2img_get_error_string(ret));
                    } else {
                        MLOG_DBG("图像处理成功！\n");
                    }
                    // 只有未被取消函数提前销毁时才自己销毁
                    if (!img2imgprocessor_destroyed && img2imgprocessor != NULL) {
                        MLOG_DBG("图像处理器未被取消，销毁图像处理器\n");
                        img2img_destroy(img2imgprocessor);
                    }
                    img2imgprocessor = NULL; // 销毁后置空
                } else {
                    MLOG_ERR("创建图生图处理器失败\n");
                    ret = -1003;
                }
            }

        AI_PROCESS_FINISH:
            // DEFALT_RETVAL 表示“仍在处理中”，不能作为最终错误码上报给UI，否则UI会一直显示处理中
            if (ret == DEFALT_RETVAL) {
                MLOG_ERR("AI图片处理返回默认等待码，转换为通用失败码\n");
                ret = -1004;
            }

            // 处理完成后再次检查是否被取消
            pthread_mutex_lock(&processor_mutex);
            if (is_thread_start == AI_PROCESS_IDLE) {
                MLOG_DBG("AI处理中途被取消，删除生成的文件\n");
                // 删除可能已生成的部分文件
                if (strlen(g_output_img_path) > 0 && access(g_output_img_path, F_OK) == 0) {
                    remove(g_output_img_path);
                    MLOG_DBG("删除已生成的部分文件: %s\n", g_output_img_path);
                }
                memset(g_output_img_path, 0, sizeof(g_output_img_path));
                memset(g_output_thumb_path, 0, sizeof(g_output_thumb_path)); // 同时清空缩略图路径
                g_api_retval = DEFALT_RETVAL;
            } else {
                if (ret == 0) {
                    result_thumbnail_save(g_output_img_path); // 保存缩略图
                } else {
                    // 处理失败时不再给不存在/无效的结果图生成缩略图，避免UI误判和重复报错
                    if (strlen(g_output_img_path) > 0 && access(g_output_img_path, F_OK) == 0) {
                        remove(g_output_img_path);
                        MLOG_DBG("删除AI处理失败产生的无效文件: %s\n", g_output_img_path);
                    }
                    memset(g_output_img_path, 0, sizeof(g_output_img_path));
                    memset(g_output_thumb_path, 0, sizeof(g_output_thumb_path));
                }
                g_api_retval = ret;
            }
            is_thread_start = AI_PROCESS_IDLE;
            pthread_mutex_unlock(&processor_mutex);
        } else {
            pthread_mutex_unlock(&processor_mutex);
        }
    }

    return NULL;
}

// #endregion
// #############################################################################
// ! #region 7. 按键、手势、定时器 等事件回调函数
// #############################################################################

// #endregion
// #############################################################################
// ! #region 8. 初始化、去初始化、资源管理
// #############################################################################

// #endregion
// #############################################################################
// ! #region 9. 调试与测试
// #############################################################################

// #endregion
// #endif
