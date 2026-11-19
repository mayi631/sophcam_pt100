/* /Misc 命令组内部接口 */
#ifndef __JL25_MISC_H__
#define __JL25_MISC_H__

#include <stdint.h>

/* 刷新心跳时间戳（每个命令到达时调用） */
void jl25_alive_refresh(void);

/* 检查 APP 心跳是否超时。返回 1=超时 0=正常 */
int32_t JL25_AliveTimeoutCheck(uint32_t timeout_sec);

#endif /* __JL25_MISC_H__ */
