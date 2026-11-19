/**
 * facep_service.c - 人脸检测与表情识别服务
 *
 * 功能概述:
 *   - 人脸检测 (Face Detection): 使用SCRFD模型检测视频流中的人脸
 *   - 人脸属性分析 (Face Attribute): 分析人脸表情(微笑检测)、性别、年龄等
 *   - 人脸框绘制: 在OSD层绘制检测到的人脸区域
 *
 * 服务架构:
 *   - FACEP_SERVICE_Create: 创建服务实例，初始化TDL模型和推理句柄
 *   - facep_task_entry: 主处理线程，持续从VPSS获取帧并执行AI推理
 *   - FACEP_SERVICE_Destory: 销毁服务，释放资源
 *
 * 线程流程 (facep_task_entry):
 *   1. 检查暂停状态，若暂停则休眠等待
 *   2. 从VPSS通道获取视频帧
 *   3. 包装帧数据为TDLImage格式
 *   4. 执行人脸检测 (若fd_enable)
 *   5. 微笑检测逻辑:
 *      - 每100ms循环一次，持续进行人脸检测
 *      - 笑脸检测需要距离上次笑脸检测500ms (smile_wait_circles=5)
 *      - 检测到表情为happy(emotion==3)立即触发拍照
 *      - 拍照后等待2s才进行下次检测 (photo_cooldown_circles=20)
 *   6. 释放视频帧和TDL资源
 *   7. 绘制或清除人脸检测框
 *   8. 休眠100ms后进入下一轮循环
 */
#include "facep_service.h"
#include "c_apis/tdl_sdk.h"
#include "cvi_vpss.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

typedef CVI_S32 (*PfpFaceDetInferFunc)(TDLHandle handle, const TDLModel model_id, TDLImage image_handle, TDLFace* obj);
typedef CVI_S32 (*PfpFaceAttrInferFunc)(TDLHandle handle, const TDLModel model_id, TDLImage image_handle, TDLFace* face_meta);

typedef struct _FACEP_CONTEXT_T {
    CVI_S32 handle;
    pthread_mutex_t mutex;
    CVI_S32 is_running;
    OSAL_TASK_HANDLE_S facep_task_handle;
    FACEP_SERVICE_PARAM_S facep_param;
    TDLHandle fp_tdl_handle;
    PfpFaceDetInferFunc fd_infer_func;
    PfpFaceAttrInferFunc fattr_infer_func;
} FACEP_CONTEXT_T;

#define MAX_FACEP_CNT MAX_CAMERA_INSTANCES
#define MAX_FACE_NUM 1
FACEP_CONTEXT_T g_facep_context[MAX_FACEP_CNT];
CVI_S32 g_facep_is_pause = 0;
CVI_S32 g_facep_is_pause_ok = 1;
RECT_S face_rects[MAX_FACE_NUM] = { 0 };
DrawRectFun g_draw_rect_func = NULL;
SmileFun g_smile_func = NULL;
SmileFun g_smile_pre_func = NULL;
SmileFun g_smile_post_func = NULL;
FACEP_SERVICE_HANDLE_T g_facep_handle_map[MAX_FACEP_CNT] = { 0 };

/**
 * facep_get_timestamp_ms - 获取当前时间戳(毫秒)
 *
 * 返回: 当前时间戳(毫秒)
 */
// static CVI_U64 facep_get_timestamp_ms(void)
// {
//     struct timeval tv;
//     gettimeofday(&tv, NULL);
//     return (CVI_U64)tv.tv_sec * 1000 + tv.tv_usec / 1000;
// }

/**
 * facep_get_model_info - 根据模型文件路径获取对应的TDL模型ID
 * @model_path: 模型文件路径字符串
 * @model_index: [输出] 获取到的模型ID
 *
 * 通过字符串匹配确定模型类型:
 *   - 包含"scrfd_det_face" -> TDL_MODEL_SCRFD_DET_FACE (人脸检测模型)
 *   - 包含"cls_attribute_gender_age_glass_emotion" -> TDL_MODEL_CLS_ATTRIBUTE_GENDER_AGE_GLASS_EMOTION (人脸属性模型)
 *
 * 返回: 成功返回CVI_SUCCESS, 失败返回CVI_FAILURE
 */
static CVI_S32 facep_get_model_info(CVI_CHAR* model_path, TDLModel* model_index)
{
    CVI_S32 ret = CVI_SUCCESS;
    if (strstr(model_path, "scrfd_det_face") != NULL) {
        *model_index = TDL_MODEL_SCRFD_DET_FACE;
    } else if (strstr(model_path, "cls_attribute_gender_age_glass_emotion") != NULL) {
        *model_index = TDL_MODEL_CLS_ATTRIBUTE_GENDER_AGE_GLASS_EMOTION;
    } else {
        ret = CVI_FAILURE;
    }
    return ret;
}

/**
 * facep_infer_func_get - 根据模型ID获取对应的TDL推理函数指针
 * @model_id: TDL模型ID
 *
 * 映射关系:
 *   - TDL_MODEL_SCRFD_DET_FACE -> TDL_FaceDetection (人脸检测推理函数)
 *   - TDL_MODEL_CLS_ATTRIBUTE_GENDER_AGE_GLASS_EMOTION -> TDL_FaceAttribute (人脸属性推理函数)
 *
 * 返回: 成功返回函数指针, 失败返回NULL
 */
static void* facep_infer_func_get(TDLModel model_id)
{
    void* ret = NULL;
    switch (model_id) {
    case TDL_MODEL_SCRFD_DET_FACE:
        ret = TDL_FaceDetection;
        CVI_LOGI("SCRFD_DET_FACE");
        break;

    case TDL_MODEL_CLS_ATTRIBUTE_GENDER_AGE_GLASS_EMOTION:
        ret = TDL_FaceAttribute;
        CVI_LOGI("CLS_ATTRIBUTE_GENDER_AGE_GLASS_EMOTION");
        break;

    default:
        CVI_LOGE("model id (%d) invalid!", model_id);
        ret = NULL;
        break;
    }

    return ret;
}

/**
 * emotion_to_text - 将表情编码转换为可读字符串
 * @code: 表情编码 (0-6)
 *
 * 表情编码映射:
 *   0: anger (愤怒)   1: disgust (厌恶)   2: fear (恐惧)
 *   3: happy (开心)   4: neutral (中性)   5: sad (悲伤)
 *   6: surprise (惊讶)
 *
 * 返回: 表情名称字符串, 未知编码返回"unknown"
 */
const char* emotion_to_text(int code)
{
    switch (code) {
    case 0:
        return "anger";
    case 1:
        return "disgust";
    case 2:
        return "fear";
    case 3:
        return "happy";
    case 4:
        return "neutral";
    case 5:
        return "sad";
    case 6:
        return "surprise";
    default:
        return "unknown";
    }
}

/**
 * facep_draw_rect - 根据人脸检测结果绘制人脸框
 * @osd_id: OSD图层ID
 * @osd_w: OSD图层宽度
 * @osd_h: OSD图层高度
 * @osd_mirror: 镜像模式标志 (1: 启用镜像, 需要翻转坐标)
 * @osd_flip: 翻转模式标志 (预留)
 * @face_meta: TDL人脸检测结果元数据
 *
 * 处理逻辑:
 *   - 遍历检测到的所有人脸 (最多MAX_FACE_NUM个)
 *   - 若启用镜像模式, 需要翻转X坐标 (osd_w - box.x)
 *   - 将TDL的box坐标转换为RECT_S格式
 *   - 调用注册的g_draw_rect_func回调绘制矩形框
 *
 * 坐标系说明:
 *   - TDL坐标: (x1, y1) 为左上角, (x2, y2) 为右下角
 *   - 镜像处理: x' = osd_w - x
 *   - face_rects: (s32X, s32Y) 为左上角, (u32Width, u32Height) 为右下角, 并非宽高
 */
static CVI_S32 facep_draw_rect(CVI_U32 osd_id, CVI_U32 osd_w, CVI_U32 osd_h,
    CVI_U32 osd_mirror, CVI_U32 osd_flip,
    TDLFace face_meta)
{
    CVI_U32 i = 0;
    CVI_S32 ret = CVI_SUCCESS;
    (void)osd_h;
    (void)osd_flip;

    memset(face_rects, 0, sizeof(RECT_S));

    for (i = 0; i < face_meta.size; i++) {
        if (i >= MAX_FACE_NUM) {
            break;
        }

        if (osd_mirror) {
            face_rects[i].s32X = osd_w - face_meta.info[i].box.x2; // x1
            face_rects[i].s32Y = face_meta.info[i].box.y1; // y1
            face_rects[i].u32Width = osd_w - face_meta.info[i].box.x1; // x2
            face_rects[i].u32Height = face_meta.info[i].box.y2; // y2
        } else {
            face_rects[i].s32X = face_meta.info[i].box.x1;
            face_rects[i].s32Y = face_meta.info[i].box.y1;
            face_rects[i].u32Width = face_meta.info[i].box.x2;
            face_rects[i].u32Height = face_meta.info[i].box.y2;
        }
        // CVI_LOGI("%d %d %d %d", face_rects[i].s32X, face_rects[i].s32Y, face_rects[i].u32Width, face_rects[i].u32Height);
    }

    if (i <= MAX_FACE_NUM) {
        if (g_draw_rect_func != NULL) {
            ret = g_draw_rect_func(osd_id, i, face_rects);
        }
    }

    return ret;
}

/**
 * facep_clear_rect - 清除指定OSD图层上的人脸检测框
 * @osd_id: OSD图层ID
 *
 * 通过传入空的face_meta调用facep_draw_rect来清除已绘制的人脸框
 */
static CVI_S32 facep_clear_rect(CVI_U32 osd_id)
{
    CVI_LOGI("in");
    TDLFace empty_face_meta;
    memset(&empty_face_meta, 0, sizeof(TDLFace));
    facep_draw_rect(osd_id, 0, 0, 0, 0, empty_face_meta);
    return CVI_SUCCESS;
}

/**
 * facep_task_entry - 人脸处理主线程入口函数
 *
 * 线程处理流程:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    while (is_running)                       │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 1. 检查暂停状态                                        │  │
 *   │  │    - 若暂停: 设置pause_ok=1, sleep(1)后继续           │  │
 *   │  │    - 若运行: 设置pause_ok=0, 继续后续处理             │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 2. 从VPSS获取视频帧                                   │  │
 *   │  │    CVI_VPSS_GetChnFrame (超时3000ms)                 │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 3. 包装帧数据                                         │  │
 *   │  │    TDL_WrapFrame -> TDLImage                         │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 4. 人脸检测 (若fd_enable)                             │  │
 *   │  │    fd_infer_func(...) -> TDLFace                     │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 5. 笑脸检测逻辑:                                      │  │
 *   │  │    - 每100ms循环一次                                  │  │
 *   │  │    - photo_cooldown > 0: 2s拍照冷却期，跳过检测      │  │
 *   │  │    - smile_wait > 0: 500ms间隔，未到检测时间         │  │
 *   │  │    - 同时满足以上条件 + 检测到人脸 -> 进行表情识别    │  │
 *   │  │    - emotion == 3 (happy) -> 立即触发g_smile_func() │  │
 *   │  │    - 触发后: photo_cooldown = 20 (2s冷却)            │  │
 *   │  │    - 检测一次笑脸后，smile_wait = 5 (500ms间隔)       │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 6. 释放资源                                          │  │
 *   │  │    - CVI_VPSS_ReleaseChnFrame                       │  │
 *   │  │    - TDL_DestroyImage                               │  │
 *   │  │    - TDL_ReleaseFaceMeta                            │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 7. OSD绘制/清除                                      │  │
 *   │  │    - fd_enable: 调用facep_draw_rect绘制             │  │
 *   │  │    - fd_disable: 调用facep_clear_rect清除           │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   │  ┌──────────────────────────────────────────────────────┐  │
 *   │  │ 8. 休眠100ms后进入下一轮循环                          │  │
 *   │  │    usleep(100*1000)                                 │  │
 *   │  └──────────────────────────────────────────────────────┘  │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * 状态变量说明:
 *   - smile_time_circles: 检测到微笑的帧计数(>0表示检测到)
 *   - smile_time_interval_circles: 预留，保持兼容性
 *   - smile_wait_circles: 笑脸检测间隔计数器(5帧=500ms)
 *   - photo_cooldown_circles: 拍照后冷却计数器(20帧=2s)
 *   - if_disable_need_remove_rect: FD禁用时是否需要清除矩形框的标志
 *
 * 资源管理:
 *   - 每次循环获取并释放一帧
 *   - 退出时设置g_facep_is_pause_ok = 1
 */
static CVI_VOID facep_task_entry(CVI_VOID* arg)
{
    FACEP_CONTEXT_T* pcontext = (FACEP_CONTEXT_T*)arg;
    FACEP_SERVICE_PARAM_S* pparam = &pcontext->facep_param;
    VPSS_GRP in_vpss_grp = pparam->in_vpss_grp;
    VPSS_CHN in_vpss_chn = pparam->in_vpss_chn;
    VIDEO_FRAME_INFO_S in_frame = { 0 };
    TDLImage image_handle;
    TDLFace face_meta;
    CVI_S32 ret = CVI_SUCCESS;
    CVI_U32 smile_time_circles = 0;
    CVI_S32 smile_time_interval_circles = 0;
    CVI_U32 fd_enable = 0;
    CVI_U32 fattr_enable = 0;
    CVI_U32 if_disable_need_remove_rect = 0;
    CVI_S32 smile_wait_circles = 0; /* 笑脸检测间隔计数器，5帧=500ms */
    CVI_S32 photo_cooldown_circles = 0; /* 拍照后冷却计数器，20帧=2s */

    CVI_LOGI("is running: %d", pcontext->is_running);
    while (pcontext->is_running) {
        if (g_facep_is_pause) {
            g_facep_is_pause_ok = 1;
            sleep(1);
            continue;
        } else {
            g_facep_is_pause_ok = 0;
        }
        /* shoud lock ? */
        fd_enable = pparam->fd_enable;
        fattr_enable = pparam->fattr_enable;

        ret = CVI_VPSS_GetChnFrame(in_vpss_grp, in_vpss_chn, &in_frame, 3000);
        if (ret != CVI_SUCCESS) {
            CVI_LOGE("Grp(%d)-Chn(%d) get frame failed with %#x\n", in_vpss_grp, in_vpss_chn, ret);
            usleep(100 * 1000);
            continue;
        }

        image_handle = TDL_WrapFrame((void*)&in_frame, false);
        memset(&face_meta, 0, sizeof(TDLFace));

        if (fd_enable || fattr_enable) {
            if (pcontext->fd_infer_func != NULL) {
                // CVI_LOGD("[FACE DETECT] 时间戳:%llu ms - 开始人脸检测",
                //     (unsigned long long)facep_get_timestamp_ms());
                pcontext->fd_infer_func(pcontext->fp_tdl_handle, pparam->model_fd_id, image_handle, &face_meta);
            } else {
                CVI_LOGE("fd_infer_func is NULL");
            }
            // for (CVI_U32 i = 0; i < face_meta.size; i++) {
            //     CVI_LOGI("[%d] x1:%f, y1:%f, x2:%f, y2:%f], score:%f\n",
            //     i, face_meta.info[i].box.x1, face_meta.info[i].box.y1,
            //     face_meta.info[i].box.x2, face_meta.info[i].box.y2,
            //     face_meta.info[i].score);
            // }
        }

        /* 更新定时器 */
        if (smile_time_interval_circles > 0) {
            smile_time_interval_circles--;
        }
        if (smile_wait_circles > 0) {
            smile_wait_circles--;
        }
        if (photo_cooldown_circles > 0) {
            photo_cooldown_circles--;
        }

        /* 笑脸检测逻辑:
         * - photo_cooldown > 0: 拍照后2s冷却期，跳过所有检测
         * - smile_wait_circles > 0: 距离上次笑脸检测未满500ms
         * - 只有检测到人脸且定时器允许时才进行表情识别
         */
        if (fattr_enable && face_meta.size > 0 && photo_cooldown_circles <= 0 && smile_wait_circles <= 0) {
            if (pcontext->fattr_infer_func != NULL) {
                // CVI_LOGD("[SMILE DETECT] 时间戳:%llu ms - 开始笑脸检测",
                //     (unsigned long long)facep_get_timestamp_ms());
                pcontext->fattr_infer_func(pcontext->fp_tdl_handle, pparam->model_fattr_id, image_handle, &face_meta);
            } else {
                CVI_LOGE("fd_infer_func is NULL");
            }
            /* 只使用第一个人脸 */
            CVI_U32 smile_score = (CVI_U32)face_meta.info[0].emotion_score;
            // CVI_LOGI("smile_score:%d smile_time_circles:%d", smile_score, smile_time_circles);
            if (smile_score == 3) {
                ++smile_time_circles;
            }
            /* 检测到笑脸时触发拍照 */
            if (g_smile_func != NULL && smile_time_circles > 0) {
                smile_time_circles = 0;
                photo_cooldown_circles = 20; /* 拍照后等待2s */
                if (g_smile_pre_func != NULL) {
                    g_smile_pre_func();
                }
                g_smile_func();
                if (g_smile_post_func != NULL) {
                    g_smile_post_func();
                }
            }
            /* 每次笑脸检测后都需等待500ms */
            smile_wait_circles = 5;
            // CVI_LOGI("Gender:%f Age:%d emotion:%s\n", face_meta.info[0].gender_score, (int)round(face_meta.info[0].age * 100.0), emotion_to_text(face_meta.info[0].emotion_score));
        } else {
            smile_time_circles = 0;
        }

        ret = CVI_VPSS_ReleaseChnFrame(in_vpss_grp, in_vpss_chn, &in_frame);
        if (ret != CVI_SUCCESS) {
            CVI_LOGE("Grp(%d)-Chn(%d) release frame failed with %#x", in_vpss_grp, in_vpss_chn, ret);
        }
        TDL_DestroyImage(image_handle);

        if (fd_enable) {
            if_disable_need_remove_rect = 1;
            facep_draw_rect(pparam->osd_id, pparam->in_width, pparam->in_height, pparam->osd_mirror, 0, face_meta);
        } else {
            /* 移除矩形框 */
            if (if_disable_need_remove_rect) {
                CVI_LOGI("remove last rect");
                facep_clear_rect(pparam->osd_id);
                if_disable_need_remove_rect = 0;
            }
        }

        TDL_ReleaseFaceMeta(&face_meta);
        usleep(100 * 1000);
    }
    g_facep_is_pause_ok = 1;
    CVI_LOGI("is exit");
}

/**
 * facep_param_dump - 打印人脸服务配置参数
 * @pparam: 人脸服务参数结构体指针
 *
 * 打印内容:
 *   - VPSS通道信息: in_vpss_grp, in_vpss_chn
 *   - 人脸检测配置: fd_enable, model_fd_id, model_fd_thres, model_fd_path
 *   - 人脸属性配置: fattr_enable, model_fattr_id, model_fattr_path
 */
static void facep_param_dump(FACEP_SERVICE_PARAM_S* pparam)
{
    if (pparam != NULL) {
        CVI_LOGI("in_vpss_grp:%d in_vpss_chn:%d\n",
            pparam->in_vpss_grp, pparam->in_vpss_chn);
        CVI_LOGI("fd_enable:%d model_fd_id:%d model_fd_thres:%f model_fd_path:%s\n",
            pparam->fd_enable, pparam->model_fd_id,
            pparam->model_fd_thres, pparam->model_fd_path);
        CVI_LOGI("fattr_enable:%d model_fattr_id:%d model_fattr_path:%s\n",
            pparam->fattr_enable, pparam->model_fattr_id, pparam->model_fattr_path);
    }
}

/**
 * facep_proc_init - 人脸服务初始化函数
 * @pcontext: 人脸服务上下文指针
 *
 * 初始化流程:
 *   1. 解析人脸检测模型路径 -> 获取model_fd_id
 *   2. 解析人脸属性模型路径 -> 获取model_fattr_id
 *   3. 获取对应的推理函数指针 (fd_infer_func, fattr_infer_func)
 *   4. 创建TDL推理句柄 (TDL_CreateHandle)
 *   5. 打开人脸检测模型 (TDL_OpenModel, model_fd_id)
 *   6. 打开人脸属性模型 (TDL_OpenModel, model_fattr_id)
 *   7. 设置人脸检测阈值 (TDL_SetModelThreshold)
 *
 * 返回: 成功返回CVI_SUCCESS, 失败返回CVI_FAILURE
 */
static CVI_S32 facep_proc_init(FACEP_CONTEXT_T* pcontext)
{
    CVI_LOGI("FP init ------------------> start");
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_SERVICE_PARAM_S* pparam = &pcontext->facep_param;
    TDLModel mode_id = TDL_MODEL_INVALID;

    facep_get_model_info(pparam->model_fd_path, &mode_id);
    pparam->model_fd_id = (TDLModel)mode_id;

    facep_get_model_info(pparam->model_fattr_path, &mode_id);
    pparam->model_fattr_id = (TDLModel)mode_id;

    pcontext->fd_infer_func = (PfpFaceDetInferFunc)facep_infer_func_get(pparam->model_fd_id);
    if (pcontext->fd_infer_func == NULL) {
        CVI_LOGE("unsupported model id: %d", pparam->model_fd_id);
        return CVI_FAILURE;
    }

    pcontext->fattr_infer_func = (PfpFaceAttrInferFunc)facep_infer_func_get(pparam->model_fattr_id);
    if (pcontext->fattr_infer_func == NULL) {
        CVI_LOGE("unsupported model id: %d", pparam->model_fattr_id);
        return CVI_FAILURE;
    }

    facep_param_dump(pparam);

    pcontext->fp_tdl_handle = TDL_CreateHandle(0);
    if (pcontext->fp_tdl_handle == NULL) {
        CVI_LOGE("TDL_FD_CreateHandle failed");
        return CVI_FAILURE;
    }

    ret = TDL_OpenModel(pcontext->fp_tdl_handle, pparam->model_fd_id, pparam->model_fd_path, NULL, 0);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("%s TDL_OpenModel failed with %#x!", pparam->model_fd_path, ret);
        return CVI_FAILURE;
    }

    ret = TDL_OpenModel(pcontext->fp_tdl_handle, pparam->model_fattr_id, pparam->model_fattr_path, NULL, 0);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("%s TDL_OpenModel failed with %#x!", pparam->model_fattr_path, ret);
        return CVI_FAILURE;
    }

    ret = TDL_SetModelThreshold(pcontext->fp_tdl_handle, pparam->model_fd_id, pparam->model_fd_thres);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("%s TDL_SetModelThreshold failed with %#x!", pparam->model_fd_path, ret);
        return CVI_FAILURE;
    }

    CVI_LOGI("FP init ------------------> done");
    return CVI_SUCCESS;
}

/**
 * facep_start - 启动人脸处理服务
 * @pcontext: 人脸服务上下文指针
 *
 * 启动流程:
 *   1. 检查是否已运行, 防止重复启动
 *   2. 调用facep_proc_init初始化TDL模型和推理句柄
 *   3. 设置is_running=1, g_facep_is_pause=0
 *   4. 初始化互斥锁pthread_mutex_init
 *   5. 创建人脸处理任务线程 (facep_task_entry)
 *      - 线程名: facep_%d (handle)
 *      - 优先级: OSAL_TASK_PRI_RT_MID
 *      - 栈大小: 256KB
 *
 * 返回: 成功返回CVI_SUCCESS, 失败返回CVI_FAILURE
 */
static CVI_S32 facep_start(FACEP_CONTEXT_T* pcontext)
{
    CVI_S32 ret = CVI_SUCCESS;

    CVI_LOGI("in");

    if (pcontext->is_running) {
        CVI_LOGE("FP has started");
        return CVI_SUCCESS;
    }

    ret = facep_proc_init(pcontext);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("facep_proc_init failed with %#x", ret);
        return CVI_FAILURE;
    }

    pcontext->is_running = 1;
    g_facep_is_pause = 0;
    pthread_mutex_init(&pcontext->mutex, NULL);

    OSAL_TASK_ATTR_S facep_task;
    static char facep_name[16] = { 0 };
    snprintf(facep_name, sizeof(facep_name), "facep_%d", pcontext->handle);
    facep_task.name = facep_name;
    facep_task.entry = facep_task_entry;
    facep_task.param = (void*)pcontext;
    facep_task.priority = OSAL_TASK_PRI_RT_MID;
    facep_task.detached = CVI_FALSE;
    facep_task.stack_size = 256 * 1024;
    ret = OSAL_TASK_Create(&facep_task, &pcontext->facep_task_handle);
    if (ret != OSAL_SUCCESS) {
        CVI_LOGE("facep task create failed, %#x", ret);
        return CVI_FAILURE;
    }

    return CVI_SUCCESS;
}

/**
 * facep_stop - 停止人脸处理服务
 * @pcontext: 人脸服务上下文指针
 *
 * 停止流程:
 *   1. 检查是否正在运行
 *   2. 设置is_running=0, 通知线程退出
 *   3. 等待线程结束 (OSAL_TASK_Join)
 *   4. 销毁线程句柄 (OSAL_TASK_Destroy)
 *   5. 清除OSD人脸框 (facep_clear_rect)
 *   6. 关闭TDL模型 (TDL_CloseModel x2)
 *   7. 销毁TDL句柄 (TDL_DestroyHandle)
 *   8. 销毁互斥锁 (pthread_mutex_destroy)
 *
 * 返回: 成功返回CVI_SUCCESS, 失败返回CVI_FAILURE
 */
static CVI_S32 facep_stop(FACEP_CONTEXT_T* pcontext)
{
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_SERVICE_PARAM_S* pparam = NULL;

    CVI_LOGI("in");

    if (!pcontext->is_running) {
        CVI_LOGE("FP has stoped");
        return CVI_SUCCESS;
    }

    pparam = &pcontext->facep_param;
    pcontext->is_running = 0;
    ret = OSAL_TASK_Join(pcontext->facep_task_handle);
    if (ret != OSAL_SUCCESS) {
        CVI_LOGE("phs_piv task join failed, %#x", ret);
        return CVI_FAILURE;
    }
    OSAL_TASK_Destroy(&pcontext->facep_task_handle);

    facep_clear_rect(pparam->osd_id);
    TDL_CloseModel(pcontext->fp_tdl_handle, pparam->model_fd_id);
    TDL_CloseModel(pcontext->fp_tdl_handle, pparam->model_fattr_id);
    ret = TDL_DestroyHandle(pcontext->fp_tdl_handle);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("TDL_FD_DestroyHandle failed with 0x%x!\n", ret);
        return CVI_FAILURE;
    }
    pcontext->fp_tdl_handle = NULL;
    pthread_mutex_destroy(&pcontext->mutex);

    return ret;
}

/**
 * facep_get_handle - 从句柄池中获取一个可用的服务句柄
 * @phandle: [输出] 获取到的句柄值
 *
 * 句柄管理:
 *   - 使用全局数组g_facep_handle_map[MAX_FACEP_CNT]管理句柄
 *   - 遍历数组找到值为0的槽位, 标记为1表示已占用
 *   - 最大支持MAX_FACEP_CNT (=MAX_CAMERA_INSTANCES)个实例
 *
 * 返回: 成功返回CVI_SUCCESS, 无可用句柄返回CVI_FAILURE
 */
static CVI_S32 facep_get_handle(FACEP_SERVICE_HANDLE_T* phandle)
{
    CVI_U32 i = 0;
    FACEP_SERVICE_HANDLE_T handle = -1;
    for (i = 0; i < MAX_FACEP_CNT; i++) {
        if (g_facep_handle_map[i] == 0) {
            g_facep_handle_map[i] = 1;
            handle = i;
            break;
        }
    }
    if (handle == -1) {
        CVI_LOGE("no handle");
        return CVI_FAILURE;
    } else {
        *phandle = handle;
    }
    return CVI_SUCCESS;
}

/**
 * facep_reset_handle - 释放句柄池中的指定句柄
 * @handle: 要释放的句柄值
 *
 * 将g_facep_handle_map[handle]置0, 表示该槽位空闲可用
 */
static CVI_S32 facep_reset_handle(FACEP_SERVICE_HANDLE_T handle)
{
    g_facep_handle_map[handle] = 0;
    return CVI_SUCCESS;
}

/**
 * FACEP_SERVICE_Create - 创建人脸服务实例
 * @phandle: [输出] 创建成功返回的服务句柄
 * @pparam: 人脸服务配置参数
 *
 * 创建流程:
 *   1. 调用facep_get_handle获取服务句柄
 *   2. 初始化上下文, 复制参数pparam
 *   3. 检查fd_enable和fattr_enable, 若都禁用则直接返回成功
 *   4. 调用facep_start启动服务(初始化模型并创建处理线程)
 *
 * 注意: 若两个功能都禁用, 不会创建线程但仍返回成功
 *
 * 返回: 成功返回CVI_SUCCESS, 失败返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Create(FACEP_SERVICE_HANDLE_T* phandle, FACEP_SERVICE_PARAM_S* pparam)
{
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_CONTEXT_T* pcontext = NULL;

    ret = facep_get_handle(phandle);
    if (ret != CVI_SUCCESS) {
        CVI_LOGE("get handle failed");
        return CVI_FAILURE;
    } else {
        pcontext = &g_facep_context[*phandle];
        pcontext->handle = *phandle;
        memcpy(&pcontext->facep_param, pparam, sizeof(FACEP_SERVICE_PARAM_S));
    }

    if (!pparam->fd_enable && !pparam->fattr_enable) {
        CVI_LOGE("both fd(%d) and fattr(%d) disabled", pparam->fd_enable, pparam->fattr_enable);
        g_facep_is_pause_ok = 1;
        return CVI_SUCCESS;
    } else {
        ret = facep_start(pcontext);
    }

    return ret;
}

/**
 * FACEP_SERVICE_Destory - 销毁人脸服务实例
 * @handle: 要销毁的服务句柄
 *
 * 销毁流程:
 *   1. 检查句柄有效性
 *   2. 调用facep_stop停止服务(等待线程退出, 释放资源)
 *   3. 调用facep_reset_handle释放句柄
 *
 * 返回: 成功返回CVI_SUCCESS, 无效句柄返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Destory(FACEP_SERVICE_HANDLE_T handle)
{
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_CONTEXT_T* pcontext = NULL;

    if (handle < MAX_FACEP_CNT && handle >= 0) {
        pcontext = &g_facep_context[handle];

        ret = facep_stop(pcontext);
        facep_reset_handle(handle);
    } else {
        CVI_LOGE("invalid handle:%d", handle);
        return CVI_FAILURE;
    }
    CVI_LOGI("handle %d finish.", handle);
    return ret;
}

/**
 * FACEP_SERVICE_Pause - 暂停人脸处理服务
 *
 * 暂停机制:
 *   - 设置g_facep_is_pause = 1
 *   - 等待线程进入暂停状态 (g_facep_is_pause_ok == 1)
 *   - 使用usleep(1000)轮询等待, 避免 busy-waiting
 *
 * 注意: 线程在每次循环开始时会检查暂停标志
 */
CVI_S32 FACEP_SERVICE_Pause(CVI_VOID)
{
    g_facep_is_pause = 1;
    while (g_facep_is_pause_ok == 0) {
        g_facep_is_pause = 1;
        usleep(1000);
    }
    return CVI_SUCCESS;
}

/**
 * FACEP_SERVICE_Resume - 恢复人脸处理服务
 *
 * 恢复机制:
 *   - 设置g_facep_is_pause = 0
 *   - 线程在下一次循环时会检测到暂停标志清除, 继续执行
 */
CVI_S32 FACEP_SERVICE_Resume(CVI_VOID)
{
    g_facep_is_pause = 0;
    return CVI_SUCCESS;
}

/**
 * FACEP_SERVICE_Register_DrawRects_Callback - 注册人脸框绘制回调
 * @pfun: 绘制回调函数指针 (类型: DrawRectFun)
 *
 * 回调时机: 当fd_enable使能且检测到人脸时调用
 * 回调参数: osd_id, 人脸数量, 人脸矩形框数组
 *
 * 返回: 成功返回CVI_SUCCESS, pfun为NULL返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Register_DrawRects_Callback(DrawRectFun pfun)
{
    if (pfun != NULL) {
        g_draw_rect_func = pfun;
        return CVI_SUCCESS;
    }
    return CVI_FAILURE;
}

/**
 * FACEP_SERVICE_Register_Smile_Callback - 注册微笑检测回调
 * @pfun: 微笑回调函数指针 (类型: SmileFun)
 *
 * 触发条件:
 *   - fattr_enable使能
 *   - 检测到人脸
 *   - 连续5帧以上检测到happy表情 (emotion == 3)
 *   - 距离上次触发超过20帧冷却时间
 *
 * 返回: 成功返回CVI_SUCCESS, pfun为NULL返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Register_Smile_Callback(SmileFun pfun)
{
    if (pfun != NULL) {
        g_smile_func = pfun;
        return CVI_SUCCESS;
    }
    return CVI_FAILURE;
}

/**
 * FACEP_SERVICE_Register_Smile_Pre_Callback - 注册微笑检测前置回调
 * @pfun: 前置回调函数指针 (类型: SmileFun)
 *
 * 调用时机: 在g_smile_func触发之前调用
 * 用途: 可用于触发拍照前的一些准备工作
 *
 * 返回: 成功返回CVI_SUCCESS, pfun为NULL返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Register_Smile_Pre_Callback(SmileFun pfun)
{
    if (pfun != NULL) {
        g_smile_pre_func = pfun;
        return CVI_SUCCESS;
    }
    return CVI_FAILURE;
}

/**
 * FACEP_SERVICE_Unregister_Smile_Pre_Callback - 注销微笑检测前置回调
 *
 * 将g_smile_pre_func置NULL
 */
CVI_S32 FACEP_SERVICE_Unregister_Smile_Pre_Callback(CVI_VOID)
{
    g_smile_pre_func = NULL;
    return CVI_SUCCESS;
}

/**
 * FACEP_SERVICE_Register_Smile_Post_Callback - 注册微笑检测后置回调
 * @pfun: 后置回调函数指针 (类型: SmileFun)
 *
 * 调用时机: 在g_smile_func触发之后调用
 * 用途:后的一些清理工作 可用于触发拍照
 *
 * 返回: 成功返回CVI_SUCCESS, pfun为NULL返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Register_Smile_Post_Callback(SmileFun pfun)
{
    if (pfun != NULL) {
        g_smile_post_func = pfun;
        return CVI_SUCCESS;
    }
    return CVI_FAILURE;
}

/**
 * FACEP_SERVICE_Unregister_Smile_Post_Callback - 注销微笑检测后置回调
 *
 * 将g_smile_post_func置NULL
 */
CVI_S32 FACEP_SERVICE_Unregister_Smile_Post_Callback(CVI_VOID)
{
    g_smile_post_func = NULL;
    return CVI_SUCCESS;
}

/**
 * FACEP_SERVICE_Enable - 启用指定的人脸服务功能
 * @handle: 服务句柄
 * @id: 功能ID (FACEP_FUNC_ID_FD / FACEP_FUNC_ID_FATTR / FACEP_FUNC_ID_BUTT)
 *
 * 功能说明:
 *   - FACEP_FUNC_ID_FD: 使能人脸检测
 *   - FACEP_FUNC_ID_FATTR: 使能人脸属性分析
 *   - FACEP_FUNC_ID_BUTT: 同时使能上述两个功能
 *
 * 若服务尚未运行, 会自动调用facep_start启动服务
 *
 * 返回: 成功返回CVI_SUCCESS, 无效句柄返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Enable(FACEP_SERVICE_HANDLE_T handle, FACEP_FUNC_ID_E id)
{
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_CONTEXT_T* pcontext = NULL;
    FACEP_SERVICE_PARAM_S* pparam = NULL;
    if (handle < MAX_FACEP_CNT && handle >= 0) {
        pcontext = &g_facep_context[handle];
        pparam = &pcontext->facep_param;
    } else {
        CVI_LOGE("handle(%d)", handle);
        return CVI_FAILURE;
    }
    switch (id) {
    case FACEP_FUNC_ID_FD:
        pparam->fd_enable = 1;
        break;

    case FACEP_FUNC_ID_FATTR:
        pparam->fattr_enable = 1;
        break;

    case FACEP_FUNC_ID_BUTT:
        pparam->fd_enable = 1;
        pparam->fattr_enable = 1;
        break;

    default:
        CVI_LOGE("unsupport id(%d)", id);
        break;
    }

    if (!pcontext->is_running) {
        ret = facep_start(pcontext);
    }

    return ret;
}

/**
 * FACEP_SERVICE_Disable - 禁用指定的人脸服务功能
 * @handle: 服务句柄
 * @id: 功能ID (FACEP_FUNC_ID_FD / FACEP_FUNC_ID_FATTR / FACEP_FUNC_ID_BUTT)
 *
 * 功能说明:
 *   - FACEP_FUNC_ID_FD: 禁用的人脸检测
 *   - FACEP_FUNC_ID_FATTR: 禁用的人脸属性分析
 *   - FACEP_FUNC_ID_BUTT: 同时禁用上述两个功能
 *
 * 若两个功能都被禁用且服务正在运行, 会自动调用facep_stop停止服务
 *
 * 返回: 成功返回CVI_SUCCESS, 无效句柄返回CVI_FAILURE
 */
CVI_S32 FACEP_SERVICE_Disable(FACEP_SERVICE_HANDLE_T handle, FACEP_FUNC_ID_E id)
{
    CVI_S32 ret = CVI_SUCCESS;
    FACEP_CONTEXT_T* pcontext = NULL;
    FACEP_SERVICE_PARAM_S* pparam = NULL;
    if (handle < MAX_FACEP_CNT && handle >= 0) {
        pcontext = &g_facep_context[handle];
        pparam = &pcontext->facep_param;
    } else {
        CVI_LOGE("handle(%d)", handle);
        return CVI_FAILURE;
    }
    switch (id) {
    case FACEP_FUNC_ID_FD:
        pparam->fd_enable = 0;
        break;

    case FACEP_FUNC_ID_FATTR:
        pparam->fattr_enable = 0;
        break;

    case FACEP_FUNC_ID_BUTT:
        pparam->fd_enable = 0;
        pparam->fattr_enable = 0;
        break;

    default:
        CVI_LOGE("unsupport id(%d)", id);
        break;
    }

    if (pparam->fd_enable == 0 && pparam->fattr_enable == 0 && pcontext->is_running) {
        ret = facep_stop(pcontext);
    }

    return ret;
}
