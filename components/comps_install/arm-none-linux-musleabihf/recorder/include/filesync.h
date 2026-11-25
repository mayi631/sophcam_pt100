
#ifndef FILE_SYNC_H
#define FILE_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FILESYNC_EVENT_CALLBACK)(void *, char *, uint32_t, void *, uint32_t);

typedef struct FILESYNC_EVENT_CB_S{
    void *cb;
    void *hdl;
    uint32_t event;
    void* argv0;
    uint32_t argv1;
} FILESYNC_EVENT_CB_S;

/**
 * @brief 将要同步的文件加入同步链表, 同步完成后会调用cb
 * 
 * @param filename 需要同步的文件及其文件名
 * @param cb 回调函数及其信息
 */
void FILESYNC_Push(char* filename, FILESYNC_EVENT_CB_S* cb);

/**
 * @brief 初始化, 创建同步线程
 * 
 * @return int32_t 成功返回0，失败返回其它
 */
int32_t FILESYNC_Init(void);

/**
 * @brief 去初始化, 销毁同步线程
 * 
 * @return int32_t 成功返回0，失败返回其它
 */
int32_t FILESYNC_Deinit(void);

#ifdef __cplusplus
}
#endif
#endif