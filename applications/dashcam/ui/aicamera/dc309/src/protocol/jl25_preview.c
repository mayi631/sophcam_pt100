#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jl25_internal.h"
#include "mlog.h"
#include "mode.h"
#include "preview_rtsp/jl25_preview_rtsp.h"

#define JL25_PREVIEW_MODE_WAIT_MS 100
#define JL25_PREVIEW_MODE_WAIT_CNT 50

static pthread_mutex_t s_preview_mutex = PTHREAD_MUTEX_INITIALIZER;

static int jl25_preview_wait_movie_mode(void)
{
    int i;

    for (i = 0; i < JL25_PREVIEW_MODE_WAIT_CNT; i++) {
        if (MODEMNG_GetCurWorkMode() == WORK_MODE_MOVIE && !MODEMNG_GetInProgress()) {
            return 0;
        }
        usleep(JL25_PREVIEW_MODE_WAIT_MS * 1000);
    }
    return -1;
}

static int jl25_preview_send_mode_switch(void)
{
    MESSAGE_S msg = { 0 };

    msg.topic = EVENT_MODEMNG_MODESWITCH;
    msg.arg1 = WORK_MODE_MOVIE;
    return MODEMNG_SendMessage(&msg);
}

int jl25_preview_ensure_movie_mode(void)
{
    int ret = 0;

    pthread_mutex_lock(&s_preview_mutex);
    if (MODEMNG_GetCurWorkMode() != WORK_MODE_MOVIE) {
        MLOG_INFO("jl25 preview switch mode: cur=%d target=%d\n",
            MODEMNG_GetCurWorkMode(), WORK_MODE_MOVIE);
        ret = jl25_preview_send_mode_switch();
    }
    if (ret == 0) {
        ret = jl25_preview_wait_movie_mode();
    }
    if (ret != 0) {
        MLOG_ERR("jl25 preview switch mode fail\n");
    }
    pthread_mutex_unlock(&s_preview_mutex);
    return ret;
}

int jl25_preview_start(void)
{
    int ret;

    ret = jl25_preview_ensure_movie_mode();
    if (ret != 0) {
        return ret;
    }

    pthread_mutex_lock(&s_preview_mutex);
    ret = jl25_preview_rtsp_start();
    pthread_mutex_unlock(&s_preview_mutex);
    return ret;
}

int jl25_preview_stop(void)
{
    int ret;

    pthread_mutex_lock(&s_preview_mutex);
    ret = jl25_preview_rtsp_stop();
    pthread_mutex_unlock(&s_preview_mutex);
    return ret;
}

int jl25_preview_is_active(void)
{
    return jl25_preview_rtsp_is_active();
}

void jl25_preview_build_rtsp_url(char* buf, size_t buf_sz)
{
    jl25_preview_rtsp_build_url(buf, buf_sz);
}
