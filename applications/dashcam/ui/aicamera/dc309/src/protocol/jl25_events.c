/*
 * JL25xx 协议事件订阅与转发
 *
 * 订阅 EVENTHUB 的设备事件，按协议 4.3.1 节映射为 {"Event":N} 推送到
 * TCP:8222 的所有已连接 APP 客户端。
 *
 *   设备事件                          -> 协议事件
 *   EVENT_MODEMNG_STOP_REC            -> Event 1（录影结束）
 *   EVENT_MODEMNG_CARD_AVAILABLE      -> Event 12（SD 卡插入）
 *   EVENT_MODEMNG_CARD_REMOVE         -> Event 13（SD 卡拔出）
 *   EVENT_MODEMNG_CARD_ERROR/FSERROR  -> Event 10（SD 卡错误）
 *   EVENT_MODEMNG_CARD_FULL_SPACE     -> Event 11（SD 卡满）
 *   EVENT_MODEMNG_CARD_READ_ONLY      -> Event 16（写保护）
 */
#define DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appcomm.h"
#include "filemng.h"
#include "jl25_events.h"
#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "mlog.h"
#include "mode.h"

static MW_PTR s_jl25_subscriber = NULL;
static EVENTHUB_SUBSCRIBER_S* s_jl25_sub_cfg = NULL;
static int32_t s_jl25_events_inited = 0;

static int jl25_event_cb(void* argv, EVENT_S* msg)
{
    (void)argv;

    switch (msg->topic) {
    case EVENT_MODEMNG_CARD_FORMAT_SUCCESSED:
    case EVENT_MODEMNG_CARD_FORMAT_FAILED:
    case EVENT_MODEMNG_CARD_MOUNT_FAILED:
    case EVENT_FILEMNG_SCAN_COMPLETED:
    case EVENT_FILEMNG_SCAN_FAIL:
        jl25_storage_format_event(msg->topic);
        break;

    case EVENT_MODEMNG_STOP_REC:
        JL25_PushEvent(JL25_EVENT_REC_STOPPED, NULL);
        break;

    case EVENT_MODEMNG_CARD_AVAILABLE:
        jl25_storage_format_event(msg->topic);
        JL25_PushEvent(JL25_EVENT_SD_INSERT, NULL);
        break;

    case EVENT_MODEMNG_CARD_REMOVE:
        JL25_PushEvent(JL25_EVENT_SD_REMOVE, NULL);
        break;

    case EVENT_MODEMNG_CARD_ERROR:
    case EVENT_MODEMNG_CARD_FSERROR:
        JL25_PushEvent(JL25_EVENT_SD_ERROR, NULL);
        break;

    case EVENT_MODEMNG_CARD_READ_ONLY:
        JL25_PushEvent(JL25_EVENT_SD_WRITEPROTECT, NULL);
        break;

    default:
        break;
    }
    return 0;
}

int32_t JL25_EventsInit(void)
{
    int32_t ret;
    EVENTHUB_SUBSCRIBER_S* sub;

    if (s_jl25_events_inited) {
        return 0;
    }

    /*
     * EVENTHUB_CreateSubscriber() 直接把传入的 EVENTHUB_SUBSCRIBER_S 结构体
     * 当作句柄返回（内部把 ps_subscriber 指针写回该结构体的 argv 字段），
     * 而 EVENTHUB_DestroySubscriber() 结尾会对这个句柄调用 free()。
     * 因此该结构体必须来自堆，且生命周期要覆盖到 Destroy 为止；
     * 用栈上局部变量会导致 free(栈地址) -> 堆元数据被破坏 -> 进程崩溃。
     */
    sub = (EVENTHUB_SUBSCRIBER_S*)calloc(1, sizeof(EVENTHUB_SUBSCRIBER_S));
    if (sub == NULL) {
        MLOG_ERR("jl25: alloc event subscriber fail\n");
        return -1;
    }
    snprintf(sub->asName, sizeof(sub->asName), "jl25");
    sub->argv = NULL;
    sub->new_msg_cb = jl25_event_cb;
    sub->sync = false;

    ret = EVENTHUB_CreateSubscriber(sub, &s_jl25_subscriber);
    if (ret != 0) {
        MLOG_ERR("jl25: create event subscriber fail, ret=%d\n", ret);
        free(sub);
        s_jl25_subscriber = NULL;
        return -1;
    }
    s_jl25_sub_cfg = sub;

    TOPIC_ID topics[] = {
        EVENT_MODEMNG_STOP_REC,
        EVENT_MODEMNG_CARD_AVAILABLE,
        EVENT_MODEMNG_CARD_REMOVE,
        EVENT_MODEMNG_CARD_ERROR,
        EVENT_MODEMNG_CARD_FSERROR,
        EVENT_MODEMNG_CARD_READ_ONLY,
        EVENT_MODEMNG_CARD_FORMAT_SUCCESSED,
        EVENT_MODEMNG_CARD_FORMAT_FAILED,
        EVENT_MODEMNG_CARD_MOUNT_FAILED,
        EVENT_FILEMNG_SCAN_COMPLETED,
        EVENT_FILEMNG_SCAN_FAIL,
    };
    for (uint32_t i = 0; i < sizeof(topics) / sizeof(topics[0]); i++) {
        ret = EVENTHUB_Subcribe(s_jl25_subscriber, topics[i]);
        if (ret != 0) {
            /* 事件可能未被注册，容忍单点失败 */
            MLOG_DBG("jl25: subscribe topic %#x fail, ret=%d\n",
                topics[i], ret);
        }
    }

    s_jl25_events_inited = 1;
    MLOG_DBG("jl25: events subscribed\n");
    return 0;
}

void JL25_EventsDeinit(void)
{
    if (!s_jl25_events_inited) {
        return;
    }
    /* 内部会释放 EVENTHUB_CreateSubscriber 传入的结构体（即 s_jl25_sub_cfg） */
    EVENTHUB_DestroySubscriber(s_jl25_subscriber);
    s_jl25_subscriber = NULL;
    s_jl25_sub_cfg = NULL;
    s_jl25_events_inited = 0;
}
