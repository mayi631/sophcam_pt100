/* JL25xx 协议事件订阅接口 */
#ifndef __JL25_EVENTS_H__
#define __JL25_EVENTS_H__

#include <stdint.h>

/* 订阅设备事件并转发到 TCP:8222（JL25_Start 内部调用，幂等） */
int32_t JL25_EventsInit(void);

/* 取消订阅 */
void JL25_EventsDeinit(void);

#endif /* __JL25_EVENTS_H__ */
