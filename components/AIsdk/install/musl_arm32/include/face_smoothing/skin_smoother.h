/**
 * @file skin_smoother.h
 * @brief 本地人脸磨皮组件接口 (C接口)
 *
 * 基于OpenCV实现的人脸美颜算法
 */

#ifndef __SKIN_SMOOTHER_H__
#define __SKIN_SMOOTHER_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ================================
// 错误码定义
// ================================

enum SKIN_SMOOTHER_ERROR {
    SKIN_SMOOTHER_SUCCESS = 0,           ///< 成功
    SKIN_SMOOTHER_INVALID_PARAM = -1,    ///< 无效参数
    SKIN_SMOOTHER_FILE_ERROR = -2,       ///< 文件操作错误
    SKIN_SMOOTHER_INTERNAL_ERROR = -3,   ///< 内部错误
    SKIN_SMOOTHER_MEMORY_ERROR = -4      ///< 内存错误
};

// ================================
// 处理器句柄定义
// ================================

/**
 * @brief 磨皮处理器句柄（不透明指针）
 */
typedef struct skin_smoother_processor skin_smoother_processor_t;

// ================================
// 核心接口函数
// ================================

/**
 * @brief 创建磨皮处理器实例
 *
 * @return skin_smoother_processor_t* 成功返回处理器实例，失败返回NULL
 */
skin_smoother_processor_t *skin_smoother_create();

/**
 * @brief 销毁磨皮处理器实例
 *
 * @param processor 处理器实例
 */
void skin_smoother_destroy(skin_smoother_processor_t *processor);

/**
 * @brief 处理图片文件
 *
 * @param processor 处理器实例
 * @param input_file 输入图片文件路径
 * @param output_file 输出文件路径，结果图片将保存到此文件
 * @return int 错误码，0表示成功
 */
int skin_smoother_process_file(skin_smoother_processor_t *processor, 
                              const char *input_file, 
                              const char *output_file);

#ifdef __cplusplus
}
#endif

#endif // __SKIN_SMOOTHER_H__
