#ifndef __JL25_PREVIEW_RTSP_H__
#define __JL25_PREVIEW_RTSP_H__

#include <stddef.h>

int jl25_preview_rtsp_start(void);
int jl25_preview_rtsp_stop(void);
int jl25_preview_rtsp_is_active(void);
void jl25_preview_rtsp_build_url(char* buf, size_t buf_sz);

#endif /* __JL25_PREVIEW_RTSP_H__ */
