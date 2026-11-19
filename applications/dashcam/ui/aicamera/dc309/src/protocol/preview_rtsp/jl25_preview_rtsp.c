#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "cvi_comm_venc.h"
#include "hal_wifi.h"
#include "mapi_venc.h"
#include "media_init.h"
#include "mlog.h"
#include "param.h"
#include "rtsp.h"
#include "subvideo_service.h"

#define JL25_PREVIEW_RTSP_NAME "liveRTSP/av4"
#define JL25_PREVIEW_RTSP_PORT 554
#define JL25_PREVIEW_CALLBACK_NAME "jl25_preview_rtsp"
#define JL25_PREVIEW_CAMERA_ID 0
#define JL25_PREVIEW_RTP_PORT 10000
#define JL25_PREVIEW_VIDEO_PORT 20000
#define JL25_PREVIEW_AUDIO_PORT 21000

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void* rtsp_hdl;
    int started;
    int stopping;
    int callback_registered;
    int callback_refs;
    int client_count;
    int32_t port;
    MAPI_VCODEC_E codec;
} jl25_preview_rtsp_ctx_t;

static jl25_preview_rtsp_ctx_t s_rtsp_ctx = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .port = JL25_PREVIEW_RTSP_PORT,
    .codec = MAPI_VCODEC_H264,
};

static int jl25_preview_rtsp_is_key_pack(const VENC_PACK_S* pack,
    MAPI_VCODEC_E codec)
{
    if (pack == NULL) {
        return 0;
    }

    if (codec == MAPI_VCODEC_H264) {
        return pack->DataType.enH264EType == H264E_NALU_IDRSLICE || pack->DataType.enH264EType == H264E_NALU_SPS || pack->DataType.enH264EType == H264E_NALU_PPS;
    }
    if (codec == MAPI_VCODEC_H265) {
        return pack->DataType.enH265EType == H265E_NALU_IDRSLICE || pack->DataType.enH265EType == H265E_NALU_VPS || pack->DataType.enH265EType == H265E_NALU_SPS || pack->DataType.enH265EType == H265E_NALU_PPS;
    }
    if (codec == MAPI_VCODEC_MJP || codec == MAPI_VCODEC_JPG) {
        return 1;
    }
    return 0;
}

static RTSP_VIDEO_FORMAT_E jl25_preview_rtsp_video_codec(MAPI_VCODEC_E codec)
{
    switch (codec) {
    case MAPI_VCODEC_H265:
        return RTSP_VIDEO_H265;
    case MAPI_VCODEC_MJP:
    case MAPI_VCODEC_JPG:
        return RTSP_VIDEO_MJPEG;
    case MAPI_VCODEC_H264:
    default:
        return RTSP_VIDEO_H264;
    }
}

static int jl25_preview_rtsp_fill_media_info(RTSP_MEDIA_INFO_S* media_info)
{
    PARAM_MEDIA_COMM_S mediacomm;
    PARAM_MEDIA_SPEC_S media_spec;
    PARAM_RECORD_CHN_ATTR_S* record_attr;
    MEDIA_PARAM_INIT_S* media_ctx = MEDIA_GetCtx();
    uint32_t bind_venc_id;
    int j;

    if (media_info == NULL) {
        return -1;
    }

    memset(media_info, 0, sizeof(*media_info));
    memset(&mediacomm, 0, sizeof(mediacomm));
    memset(&media_spec, 0, sizeof(media_spec));
    PARAM_GetMediaComm(&mediacomm);
    PARAM_GetMediaMode(JL25_PREVIEW_CAMERA_ID, &media_spec);

    record_attr = &mediacomm.Record.ChnAttrs[JL25_PREVIEW_CAMERA_ID];
    bind_venc_id = record_attr->Subvideoen ? record_attr->SubBindVencId : record_attr->BindVencId;

    for (j = 0; j < MAX_VENC_CNT; j++) {
        MEDIA_VENC_CHN_ATTR_S* venc_attr = &media_spec.VencAttr.VencChnAttr[j];

        if (!venc_attr->VencChnEnable || venc_attr->VencId != bind_venc_id) {
            continue;
        }
        if (media_ctx->SysHandle.venchdl[JL25_PREVIEW_CAMERA_ID][j] == NULL) {
            MLOG_ERR("jl25 preview venc handle missing, venc_id=%u idx=%d\n",
                bind_venc_id, j);
            return -1;
        }

        media_info->id = JL25_PREVIEW_CAMERA_ID;
        snprintf(media_info->rtsp_name, sizeof(media_info->rtsp_name), "%s",
            JL25_PREVIEW_RTSP_NAME);
        media_info->video_codec = jl25_preview_rtsp_video_codec(venc_attr->MapiVencAttr.codec);
        media_info->audio_codec = RTSP_AUDIO_BUTT;
        media_info->framerate = venc_attr->framerate;
        if (media_info->framerate <= 0.0f) {
            media_info->framerate = (float)venc_attr->MapiVencAttr.dst_framerate;
        }
        if (media_info->framerate <= 0.0f) {
            media_info->framerate = 25.0f;
        }
        media_info->bitrate_kbps = venc_attr->MapiVencAttr.bitrate_kbps;
        media_info->rtp_port = JL25_PREVIEW_RTP_PORT;
        media_info->video_port = JL25_PREVIEW_VIDEO_PORT;
        media_info->audio_port = JL25_PREVIEW_AUDIO_PORT;

        s_rtsp_ctx.codec = venc_attr->MapiVencAttr.codec;
        s_rtsp_ctx.port = JL25_PREVIEW_RTSP_PORT;
        return 0;
    }

    MLOG_ERR("jl25 preview sub stream venc not found, bind_venc_id=%u\n",
        bind_venc_id);
    return -1;
}

static void jl25_preview_rtsp_send_frame(const VENC_STREAM_S* venc_stream,
    void* arg)
{
    jl25_preview_rtsp_ctx_t* ctx = (jl25_preview_rtsp_ctx_t*)arg;
    RTSP_FRAME_S frame;
    int pack_count;
    int i;

    if (ctx == NULL || venc_stream == NULL || venc_stream->pstPack == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->mutex);
    if (ctx->stopping || ctx->rtsp_hdl == NULL) {
        pthread_mutex_unlock(&ctx->mutex);
        return;
    }
    ctx->callback_refs++;
    pthread_mutex_unlock(&ctx->mutex);

    memset(&frame, 0, sizeof(frame));
    frame.type = FRAME_TYPE_VIDEO;
    pack_count = (int)venc_stream->u32PackCount;
    if (pack_count > MAX_RTSP_VIDEO_FRAME) {
        pack_count = MAX_RTSP_VIDEO_FRAME;
    }

    for (i = 0; i < pack_count; i++) {
        const VENC_PACK_S* pack = &venc_stream->pstPack[i];

        if (pack->u32Len <= pack->u32Offset) {
            continue;
        }
        frame.data[i] = pack->pu8Addr + pack->u32Offset;
        frame.len[i] = pack->u32Len - pack->u32Offset;
        frame.vi_pts[i] = pack->u64PTS;
        frame.iskey[i] = jl25_preview_rtsp_is_key_pack(pack, ctx->codec);
    }

    RTSP_SendFrame(ctx->rtsp_hdl, &frame);

    pthread_mutex_lock(&ctx->mutex);
    ctx->callback_refs--;
    if (ctx->stopping && ctx->callback_refs == 0) {
        pthread_cond_signal(&ctx->cond);
    }
    pthread_mutex_unlock(&ctx->mutex);
}

static void jl25_preview_rtsp_event_cb(RTSP_EVENT_S* event)
{
    int need_set = 0;
    int need_unset = 0;

    if (event == NULL) {
        return;
    }

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    if (s_rtsp_ctx.rtsp_hdl == NULL) {
        pthread_mutex_unlock(&s_rtsp_ctx.mutex);
        return;
    }

    switch (event->e) {
    case RTSP_EVENT_CLI_CONNECT:
        s_rtsp_ctx.client_count++;
        if (!s_rtsp_ctx.callback_registered) {
            s_rtsp_ctx.callback_registered = 1;
            need_set = 1;
        }
        MLOG_INFO("jl25 preview rtsp client connected: ip=%s count=%d\n",
            event->cli_ipaddr, s_rtsp_ctx.client_count);
        break;
    case RTSP_EVENT_CLI_DISCONNECT:
        if (s_rtsp_ctx.client_count > 0) {
            s_rtsp_ctx.client_count--;
        }
        if (s_rtsp_ctx.client_count == 0 && s_rtsp_ctx.callback_registered) {
            s_rtsp_ctx.callback_registered = 0;
            need_unset = 1;
        }
        MLOG_INFO("jl25 preview rtsp client disconnected: ip=%s count=%d\n",
            event->cli_ipaddr, s_rtsp_ctx.client_count);
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);

    if (need_set) {
        if (VIDEO_SERVICR_CallbackSet(JL25_PREVIEW_CAMERA_ID,
                JL25_PREVIEW_CALLBACK_NAME,
                jl25_preview_rtsp_send_frame,
                &s_rtsp_ctx)
            != 0) {
            pthread_mutex_lock(&s_rtsp_ctx.mutex);
            s_rtsp_ctx.callback_registered = 0;
            pthread_mutex_unlock(&s_rtsp_ctx.mutex);
            MLOG_ERR("jl25 preview callback set failed\n");
        }
    }
    if (need_unset) {
        if (VIDEO_SERVICR_CallbackUnSet(JL25_PREVIEW_CAMERA_ID,
                JL25_PREVIEW_CALLBACK_NAME)
            != 0) {
            MLOG_ERR("jl25 preview callback unset failed\n");
        }
    }
}

int jl25_preview_rtsp_start(void)
{
    RTSP_INFO_S rtsp_info;
    RTSP_MEDIA_INFO_S media_info;
    int ret = 0;

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    if (s_rtsp_ctx.started) {
        pthread_mutex_unlock(&s_rtsp_ctx.mutex);
        return 0;
    }
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);

    if (jl25_preview_rtsp_fill_media_info(&media_info) != 0) {
        return -1;
    }

    memset(&rtsp_info, 0, sizeof(rtsp_info));
    rtsp_info.max_conn = 2;
    rtsp_info.timeout = 120;
    rtsp_info.port = s_rtsp_ctx.port;
    media_info.cb = jl25_preview_rtsp_event_cb;

    ret = RTSP_Create(&s_rtsp_ctx.rtsp_hdl, &rtsp_info, &media_info);
    if (ret != 0 || s_rtsp_ctx.rtsp_hdl == NULL) {
        MLOG_ERR("jl25 preview rtsp create failed, ret=%d\n", ret);
        s_rtsp_ctx.rtsp_hdl = NULL;
        return -1;
    }

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    s_rtsp_ctx.started = 1;
    s_rtsp_ctx.stopping = 0;
    s_rtsp_ctx.callback_registered = 0;
    s_rtsp_ctx.callback_refs = 0;
    s_rtsp_ctx.client_count = 0;
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);

    MLOG_INFO("jl25 preview local rtsp started: url=rtsp://%s:%d/%s\n",
        HAL_WIFI_IP, s_rtsp_ctx.port, JL25_PREVIEW_RTSP_NAME);
    return 0;
}

int jl25_preview_rtsp_stop(void)
{
    void* rtsp_hdl;
    int need_unset = 0;

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    if (!s_rtsp_ctx.started) {
        pthread_mutex_unlock(&s_rtsp_ctx.mutex);
        return 0;
    }
    s_rtsp_ctx.stopping = 1;
    need_unset = s_rtsp_ctx.callback_registered;
    s_rtsp_ctx.callback_registered = 0;
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);

    if (need_unset) {
        VIDEO_SERVICR_CallbackUnSet(JL25_PREVIEW_CAMERA_ID,
            JL25_PREVIEW_CALLBACK_NAME);
    }

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    while (s_rtsp_ctx.callback_refs > 0) {
        pthread_cond_wait(&s_rtsp_ctx.cond, &s_rtsp_ctx.mutex);
    }
    rtsp_hdl = s_rtsp_ctx.rtsp_hdl;
    s_rtsp_ctx.rtsp_hdl = NULL;
    s_rtsp_ctx.started = 0;
    s_rtsp_ctx.client_count = 0;
    s_rtsp_ctx.stopping = 0;
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);

    if (rtsp_hdl != NULL) {
        RTSP_Destroy(rtsp_hdl);
    }

    MLOG_INFO("jl25 preview local rtsp stopped\n");
    return 0;
}

int jl25_preview_rtsp_is_active(void)
{
    int active;

    pthread_mutex_lock(&s_rtsp_ctx.mutex);
    active = s_rtsp_ctx.started;
    pthread_mutex_unlock(&s_rtsp_ctx.mutex);
    return active;
}

void jl25_preview_rtsp_build_url(char* buf, size_t buf_sz)
{
    if (buf == NULL || buf_sz == 0) {
        return;
    }
    snprintf(buf, buf_sz, "rtsp://%s:%d/%s", HAL_WIFI_IP,
        s_rtsp_ctx.port > 0 ? s_rtsp_ctx.port : JL25_PREVIEW_RTSP_PORT,
        JL25_PREVIEW_RTSP_NAME);
}
