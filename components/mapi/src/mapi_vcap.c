#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "cvi_log.h"
#include "mapi.h"
#include "mapi_vcap_internal.h"
#include "osal.h"

#include "cvi_ae.h"
#include "cvi_af.h"
#include "cvi_awb.h"
#include "cvi_bin.h"
#include "cvi_buffer.h"
#include "cvi_sys.h"
#include "cvi_vb.h"
#include "hal_gpio.h"

#define CHECK_MAPI_VCAP_NULL_PTR_RET(ptr)           \
    do {                                            \
        if (ptr == NULL) {                          \
            CVI_LOGE("%s is NULL pointer\n", #ptr); \
            return MAPI_ERR_INVALID;                \
        }                                           \
    } while (0)

#define CHECK_MAPI_VCAP_MAX_VAL_RET(paraname, value, max)                             \
    do {                                                                              \
        if (value > max) {                                                            \
            CVI_LOGE("%s:%d can not larger than max val %d\n", paraname, value, max); \
            return MAPI_ERR_INVALID;                                                  \
        }                                                                             \
    } while (0)

#define CHECK_MAPI_VCAP_ZERO_VAL_RET(paraname, value)                   \
    do {                                                                \
        if (value == 0) {                                               \
            CVI_LOGE("%s is zero\n", paraname);                         \
            return MAPI_ERR_INVALID;                                \
        }                                                               \
    } while (0)

#define CHECK_MAPI_VCAP_RET(ret, fmt...)                                \
    do {                                                                \
        if (ret != CVI_SUCCESS) {                                       \
            CVI_LOGE(fmt);                                              \
            CVI_LOGE("fail and return:[%#x]\n", ret);                   \
            return ret;                                                 \
        }                                                               \
    } while (0)

typedef MAPI_HANDLE_T MAPI_VCAP_HANDLE_T;

typedef struct VCAP_DUMP_RAW_CTX_S {
    CVI_BOOL bStart;
    VI_PIPE ViPipe;
    CVI_U32 u32Count;
    MAPI_VCAP_RAW_DATA_T stCallbackFun;
    pthread_t pthreadDumpRaw;
} VCAP_DUMP_RAW_CTX_T;

typedef struct MAPI_VCAP_SENSOR_S {
    int sns_id;
    MAPI_VCAP_ATTR_T attr;
    MAPI_VCAP_HANDLE_T vcap_hdl;
    VCAP_DUMP_RAW_CTX_T stVcapDumpRawCtx;
} MAPI_VCAP_SENSOR_T;

typedef struct _MAPI_SNS_INFO_S {
    SENSOR_TYPE_E enSnsType;
    CVI_S32 s32SnsId;
    CVI_S32 s32BusId;
    CVI_S32 s32SnsI2cAddr;
    CVI_U32 MipiDev;
    CVI_S16 as16LaneId[5];
    CVI_S8  as8PNSwap[5];
    CVI_S8  aPipe[VI_MAX_PIPE_NUM];
    CVI_U8  u8HwSync;
    CVI_U8  u8CamClkId;
    CVI_U8  u8RstGpioInx;
    CVI_U8  u8RstGpioPin;
    CVI_U8  u8RstGpioPol;
} MAPI_SNS_INFO_S;

typedef struct _MAPI_CHN_INFO_S {
    CVI_S32 s32ChnId;
    CVI_U32 u32Width;
    CVI_U32 u32Height;
    CVI_FLOAT f32Fps;
    PIXEL_FORMAT_E enPixFormat;
    WDR_MODE_E enWDRMode;
    COMPRESS_MODE_E enCompressMode;
    CVI_U32 vbcnt;
    VB_POOL bindVbPool;
} MAPI_CHN_INFO_S;

typedef struct _MAPI_VI_INFO_S {
    MAPI_SNS_INFO_S stSnsInfo;
    MAPI_CHN_INFO_S stChnInfo;
} MAPI_VI_INFO_S;

typedef struct _MAPI_VI_CONFIG_S {
    MAPI_VI_INFO_S astViInfo[VI_MAX_DEV_NUM];
    CVI_S32 s32WorkingViNum;
} MAPI_VI_CONFIG_S;

typedef struct MAPI_VCAP_CTX_S {
    MAPI_VI_CONFIG_S ViConfig;
    #ifdef CHIP_184X
    SENSOR_CFG_S stSnsCfg;
    #endif
    int ref_count;
} MAPI_VCAP_CTX_T;

ISP_PUB_ATTR_S isp_pub_attr_base = {
    .stWndRect = {0, 0, 1920, 1080},
    .stSnsSize = {1920, 1080},
    .f32FrameRate = 25.0f,
    .enBayer = BAYER_BGGR,
    .enWDRMode = WDR_MODE_NONE,
    .u8SnsMode = 0,
};

VI_DEV_ATTR_S vi_dev_attr_base = {
    .enIntfMode = VI_MODE_MIPI,
    .enWorkMode = VI_WORK_MODE_1Multiplex,
    .enScanMode = VI_SCAN_PROGRESSIVE,
    .as32AdChnId = {-1, -1, -1, -1},
    .enDataSeq = VI_DATA_SEQ_YUYV,
    .stSynCfg = {
    /*port_vsync    port_vsync_neg      port_hsync              port_hsync_neg*/
    VI_VSYNC_PULSE, VI_VSYNC_NEG_LOW, VI_HSYNC_VALID_SINGNAL, VI_HSYNC_NEG_HIGH,
    /*port_vsync_valid       port_vsync_valid_neg*/
    VI_VSYNC_VALID_SIGNAL, VI_VSYNC_VALID_NEG_HIGH,

    /*hsync_hfb  hsync_act    hsync_hhb*/
    {0,           1280,       0,
    /*vsync0_vhb vsync0_act vsync0_hhb*/
     0,           720,           0,
    /*vsync1_vhb vsync1_act vsync1_hhb*/
     0,            0,          0}
    },
    .enInputDataType = VI_DATA_TYPE_RGB,
    .stSize = {1280, 720},
    .stWDRAttr = {WDR_MODE_NONE, 720},
    .enBayerFormat = BAYER_FORMAT_BG,
};

VI_PIPE_ATTR_S vi_pipe_attr_base = {
    .enPipeBypassMode = VI_PIPE_BYPASS_NONE,
    .bYuvSkip = CVI_FALSE,
    .bIspBypass = CVI_FALSE,
    .u32MaxW = 1280,
    .u32MaxH = 720,
    .enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP,
    .enCompressMode = COMPRESS_MODE_TILE,
    .enBitWidth = DATA_BITWIDTH_12,
    .bNrEn = CVI_FALSE,
    .bSharpenEn = CVI_FALSE,
    .stFrameRate = {-1, -1},
    .bDiscardProPic = CVI_FALSE,
    .bYuvBypassPath = CVI_FALSE,
};


VI_CHN_ATTR_S vi_chn_attr_base = {
    .stSize = {1280, 720},
    .enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_420,
    .enDynamicRange = DYNAMIC_RANGE_SDR8,
    .enVideoFormat = VIDEO_FORMAT_LINEAR,
    .enCompressMode = COMPRESS_MODE_TILE,
    .bMirror = CVI_FALSE,
    .bFlip = CVI_FALSE,
    .u32Depth = 0,
    .stFrameRate = {-1, -1},
};

#define MAPI_ISP_BYPASS_CNT  10

static MAPI_VCAP_HANDLE_T vcap_ctx = NULL;
static pthread_mutex_t vcap_mutex = PTHREAD_MUTEX_INITIALIZER;
SENSOR_TYPE_E enSnsType[VI_MAX_DEV_NUM] = {SENSOR_NONE, SENSOR_NONE};

#ifdef ENABLE_ISP_PQ_TOOL
#include "cvi_ispd2.h"
static CVI_BOOL g_ISPDaemon = CVI_FALSE;
static CVI_BOOL g_RawDump = CVI_FALSE;
#define ISPD_CONNECT_PORT 5566
#endif

#ifndef CHIP_184X
CVI_S32 MAPI_VCAP_GetDevAttr(SENSOR_TYPE_E enSnsType, VI_DEV_ATTR_S *pstViDevAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    CVI_S32 enWdrMode = pstViDevAttr->stWDRAttr.enWDRMode;

    memcpy(pstViDevAttr, &vi_dev_attr_base, sizeof(VI_DEV_ATTR_S));

    switch (enSnsType) {
    case SONY_IMX327_MIPI_2M_30FPS_12BIT:
    case SONY_IMX335_MIPI_5M_30FPS_12BIT:
    case SONY_IMX307_MIPI_2M_30FPS_12BIT:
    case SONY_IMX307_2L_MIPI_2M_30FPS_12BIT:
    case SONY_IMX307_SLAVE_MIPI_2M_30FPS_12BIT:
    case GCORE_GC02M1_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2053_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2053_1L_MIPI_2M_30FPS_10BIT:
    case GCORE_GC1054_MIPI_1M_30FPS_10BIT:
    case GCORE_GC4023_MIPI_4M_30FPS_10BIT:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        break;
    case GCORE_GC4653_MIPI_4M_30FPS_10BIT:
    case GCORE_GC1084_MIPI_1M_30FPS_10BIT:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_GR;
        break;
    case OV_OS5648_4M_15FPS_8BIT:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        break;
    case PIXELPLUS_XS9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9951_2M_25FPS_8BIT:
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_VYUY;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        pstViDevAttr->enIntfMode = VI_MODE_BT656;
        break;
    case CISTA_C2395_MIPI_2M_30FPS_10BIT:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_BG;
        if(enWdrMode == WDR_MODE_2To1_LINE){
            //pstViDevAttr->enVcWdrMode = 1;
        }
        break;
    // YUV Sensor
    case PIXELPLUS_PR2020_2M_25FPS_8BIT:
    case NEXTCHIP_N5_2M_25FPS_8BIT:
        pstViDevAttr->enDataSeq = VI_DATA_SEQ_YUYV;
        pstViDevAttr->enInputDataType = VI_DATA_TYPE_YUV;
        pstViDevAttr->enIntfMode = VI_MODE_BT656;
        break;

    case GCORE_GC2093_MIPI_2M_30FPS_10BIT:
        pstViDevAttr->enBayerFormat = BAYER_FORMAT_RG;
        if(enWdrMode == WDR_MODE_2To1_LINE){
            //pstViDevAttr->enVcWdrMode = 1;
        }
        break;
    default:
        printf("get sensor %d attr failed!\n", enSnsType);
        s32Ret = CVI_FAILURE;
        break;
    };

    pstViDevAttr->stWDRAttr.enWDRMode = enWdrMode;

    return s32Ret;
}

CVI_S32 MAPI_VCAP_GetPipeAttr(SENSOR_TYPE_E enSnsType, VI_PIPE_ATTR_S *pstViPipeAttr)
{
    memcpy(pstViPipeAttr, &vi_pipe_attr_base, sizeof(VI_PIPE_ATTR_S));

    switch (enSnsType) {
    case PIXELPLUS_PR2020_2M_25FPS_8BIT:
    case NEXTCHIP_N5_2M_25FPS_8BIT:
    case TECHPOINT_TP9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9951_2M_25FPS_8BIT:
        pstViPipeAttr->bYuvBypassPath = CVI_TRUE;
        break;
    default:
        pstViPipeAttr->bYuvBypassPath = CVI_FALSE;
        break;
    }

    return CVI_SUCCESS;
}

CVI_S32 MAPI_VCAP_GetChnAttr(SENSOR_TYPE_E enSnsType, VI_CHN_ATTR_S *pstViChnAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstViChnAttr, &vi_chn_attr_base, sizeof(VI_CHN_ATTR_S));

    switch (enSnsType) {

    case SONY_IMX327_MIPI_2M_30FPS_12BIT:
    case SONY_IMX335_MIPI_5M_30FPS_12BIT:
    case SONY_IMX307_MIPI_2M_30FPS_12BIT:
    case SONY_IMX307_2L_MIPI_2M_30FPS_12BIT:
    case SONY_IMX307_SLAVE_MIPI_2M_30FPS_12BIT:
    case GCORE_GC02M1_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2053_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2053_1L_MIPI_2M_30FPS_10BIT:
    case GCORE_GC1054_MIPI_1M_30FPS_10BIT:
    case GCORE_GC2093_MIPI_2M_30FPS_10BIT:
    case GCORE_GC4653_MIPI_4M_30FPS_10BIT:
    case GCORE_GC4023_MIPI_4M_30FPS_10BIT:
    case GCORE_GC1084_MIPI_1M_30FPS_10BIT:
    case CISTA_C4390_MIPI_4M_30FPS_10BIT:
    case CISTA_C4395_MIPI_4M_30FPS_10BIT:
    case CISTA_C2395_MIPI_2M_30FPS_10BIT:
    case OV_OS5648_4M_15FPS_8BIT:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_NV21;
        break;
    // YUV Sensor
    case PIXELPLUS_PR2020_2M_25FPS_8BIT:
    case TECHPOINT_TP9950_2M_25FPS_8BIT:
    case NEXTCHIP_N5_2M_25FPS_8BIT:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_NV21;
        break;
    case PIXELPLUS_XS9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9951_2M_25FPS_8BIT:
        pstViChnAttr->enPixelFormat = PIXEL_FORMAT_VYUY;
        break;
    default:
        printf("get chn %d attr failed!\n", enSnsType);
        s32Ret = CVI_FAILURE;
        break;
    };

    return s32Ret;
}

CVI_S32 MAPI_VCAP_GetIspPubAttr(SENSOR_TYPE_E enSnsType, ISP_PUB_ATTR_S *pstIspPubAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    memcpy(pstIspPubAttr, &isp_pub_attr_base, sizeof(ISP_PUB_ATTR_S));

    switch (enSnsType) {
    case GCORE_GC1054_MIPI_1M_30FPS_10BIT:
    case GCORE_GC2053_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2053_1L_MIPI_2M_30FPS_10BIT:
    case GCORE_GC2093_MIPI_2M_30FPS_10BIT:
    case GCORE_GC4023_MIPI_4M_30FPS_10BIT:
    case SONY_IMX335_MIPI_5M_30FPS_12BIT:
    case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        pstIspPubAttr->enBayer = BAYER_RGGB;
        break;
    case GCORE_GC4653_MIPI_4M_30FPS_10BIT:
    case GCORE_GC1084_MIPI_1M_30FPS_10BIT:
        pstIspPubAttr->enBayer = BAYER_GRBG;
        break;
    case PIXELPLUS_PR2020_2M_25FPS_8BIT:
    case NEXTCHIP_N5_2M_25FPS_8BIT:
    case TECHPOINT_TP9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9950_2M_25FPS_8BIT:
    case PIXELPLUS_XS9951_2M_25FPS_8BIT:
    case CISTA_C4390_MIPI_4M_30FPS_10BIT:
    case CISTA_C4395_MIPI_4M_30FPS_10BIT:
    case CISTA_C2395_MIPI_2M_30FPS_10BIT:
    case OV_OS5648_4M_15FPS_8BIT:
        pstIspPubAttr->enBayer = BAYER_BGGR;
        break;
    default:
        s32Ret = CVI_FAILURE;
        break;
    }
    return s32Ret;
}
#endif

static char PqBinName[WDR_MODE_MAX][BIN_FILE_LENGTH] = { "/mnt/data/bin/cvi_sdr_bin", "/mnt/data/bin/cvi_sdr_bin",
							   "/mnt/data/bin/cvi_sdr_bin", "/mnt/data/bin/cvi_wdr_bin",
							   "/mnt/data/bin/cvi_wdr_bin", "/mnt/data/bin/cvi_wdr_bin",
							   "/mnt/data/bin/cvi_wdr_bin", "/mnt/data/bin/cvi_wdr_bin",
							   "/mnt/data/bin/cvi_wdr_bin", "/mnt/data/bin/cvi_wdr_bin",
							   "/mnt/data/bin/cvi_wdr_bin", "/mnt/data/bin/cvi_wdr_bin" };

static enum CVI_BIN_SECTION_ID s_BinId[4] = {CVI_BIN_ID_ISP0, CVI_BIN_ID_ISP1, CVI_BIN_ID_ISP2, CVI_BIN_ID_ISP3};

static CVI_S32 _getFileSize(FILE *fp, CVI_U32 *size)
{
	CVI_S32 ret = CVI_SUCCESS;

	fseek(fp, 0L, SEEK_END);
	*size = ftell(fp);
	rewind(fp);

	return ret;
}

CVI_S32 MAPI_VCAP_BIN_ReadParaFrombin(enum CVI_BIN_SECTION_ID id)
{
	CVI_S32 ret = CVI_SUCCESS;
	FILE *fp = NULL;
	CVI_U8 *buf = NULL;
	CVI_CHAR binName[BIN_FILE_LENGTH] = {0};
	CVI_U32 u32file_size = 0;

	ret = CVI_BIN_GetBinName(binName);
	if (ret != CVI_SUCCESS) {
		CVI_LOGE("GetBinName(%s) fail\n", binName);
	}

	fp = fopen((const CVI_CHAR *)binName, "rb");
	if (fp == NULL) {
		if (id == CVI_BIN_ID_VPSS) {
			CVI_LOGE("Can't find bin(%s)\n", binName);
		} else if (id >= CVI_BIN_ID_ISP0 && id <= CVI_BIN_ID_ISP3) {
			CVI_LOGE("Can't find bin(%s), use default parameters\n", binName);
		} else {
			CVI_LOGE("Can't find bin(%s)\n", binName);
		}
		ret = CVI_FAILURE;
		goto ERROR_HANDLER;
	} else {
		CVI_LOGD("Bin exist (%s)\n", binName);
	}
	_getFileSize(fp, &u32file_size);

	buf = (CVI_U8 *)malloc(u32file_size);
	if (buf == NULL) {
		ret = CVI_FAILURE;
		CVI_LOGE("Allocate memory fail\n");
		goto ERROR_HANDLER;
	}
	fread(buf, u32file_size, 1, fp);

    #ifdef CHIP_184X
    ret = CVI_BIN_LoadParamFromBinEx(id, buf, u32file_size);
	if(ret != CVI_SUCCESS){
		CVI_LOGE("load %s failed: 0x%x\n", binName, ret);
	}else{
		CVI_LOGD("load %s success\n", binName);
	}
    #else
	if (id >= CVI_BIN_ID_ISP0 && id <= CVI_BIN_ID_ISP3) {
		ret = CVI_BIN_LoadParamFromBin(CVI_BIN_ID_HEADER, buf);
		if (ret != CVI_SUCCESS) {
			CVI_LOGE("Bin Version not match, use default parameters\n");
			goto ERROR_HANDLER;
		}
	}
	ret = CVI_BIN_LoadParamFromBin(id, buf);
    if (id == CVI_BIN_ID_ISP0) {
        ret = CVI_BIN_LoadParamFromBin(CVI_BIN_ID_VPSS, buf);
        ret = CVI_BIN_LoadParamFromBin(CVI_BIN_ID_VO, buf);
    }
    #endif
ERROR_HANDLER:
	if (fp != NULL) {
		fclose(fp);
	}
	if (buf != NULL) {
		free(buf);
	}

	return ret;
}

#ifdef DUAL_OS
int MAPI_VCAP_SetAhdMode(int sns_id, int mode)
{
    #ifdef CHIP_184X
    return CVI_SNS_SetAHDMode(sns_id, mode);
    #else
    return CVI_SENSOR_SetAHDMode(sns_id, mode);
    #endif
}

#ifdef CHIP_184X
int MAPI_VCAP_SetAhdInit(int sns_id, bool isFirstInit)
{
    return CVI_SNS_SetAHDInit(sns_id, isFirstInit);
}
#endif

int MAPI_VCAP_GetAhdMode(int sns_id, int *mode, int *status, int8_t sensortype)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    enSnsType[sns_id] = sensortype;//get_sensor_type(sns_id);
    if (enSnsType[sns_id] == SENSOR_NONE) {
        CVI_LOGE("sensor[%d] dev type error\n", sns_id);
        return MAPI_ERR_INVALID;
    }

    #ifndef CHIP_184X
    s32Ret = CVI_SENSOR_SetSnsType(sns_id, enSnsType[sns_id]);
    #endif
    CHECK_MAPI_VCAP_RET(s32Ret, "SENSOR_SetSnsType(%d) failed!\n", sns_id);

    SNS_STATUS_MSG_S sstatus = {
        .s32SnsId = sns_id,
        .s32Status = 0,
        .eMode = AHD_MODE_NONE
    };
    #ifndef CHIP_184X
    s32Ret = CVI_SENSOR_GetAhdStatus(&sstatus);
    #else
    s32Ret = CVI_SNS_GetAhdStatus(&sstatus);
    #endif
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("SENSOR_GetAhdStatus failed!\n");
        return MAPI_ERR_INVALID;
    }
    *status = sstatus.s32Status;
    *mode = sstatus.eMode;

    return CVI_SUCCESS;
}

int MAPI_VCAP_InitSensorDetect(int sns_id, void *cb)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    if(cb == NULL){
        CVI_LOGE("MAPI_VCAP_InitSensorDetect failed!\n");
        return MAPI_ERR_INVALID;
    }

    #ifndef CHIP_184X
    CVI_SENSOR_AHDRegisterDetect((AHD_Callback)cb);
    s32Ret = CVI_SENSOR_EnableDetect(sns_id);
    #else
    s32Ret = CVI_SNS_AHDRegisterDetect(sns_id, (AHD_Callback)cb);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_SNS_AHDRegisterDetect failed!\n");
        return MAPI_ERR_INVALID;
    }
    s32Ret = CVI_SNS_EnableDetect(sns_id);
    #endif
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("Sensor EnableDetect failed!\n");
        return MAPI_ERR_INVALID;
    }
    return CVI_SUCCESS;
}
#else
static ISP_SNS_OBJ_S *_MAPI_VCAP_Get_SnsObj(SENSOR_TYPE_E enSnsType)
{
    switch (enSnsType) {
    #ifdef SENSOR_GCORE_GC4653
        case GCORE_GC4653_MIPI_4M_30FPS_10BIT:
            return &stSnsGc4653_Obj;
    #endif
    default:
        return CVI_NULL;
    }
}
#endif

#ifndef CHIP_184X
static int _MAPI_VCAP_StartSensor(MAPI_VI_INFO_S *VI_Info, int sns_id)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    MAPI_VI_INFO_S *pstViInfo = VI_Info;

    ISP_SNS_CFG_S isp_sns_cfg;
    VI_PIPE_ATTR_S stViPipeAttr;

    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr(%d) failed!\n", sns_id);

    isp_sns_cfg.stSnsSize.u32Width = pstViInfo->stChnInfo.u32Width;
    isp_sns_cfg.stSnsSize.u32Height = pstViInfo->stChnInfo.u32Height;
    isp_sns_cfg.f32FrameRate = pstViInfo->stChnInfo.f32Fps;
    isp_sns_cfg.enWDRMode = pstViInfo->stChnInfo.enWDRMode;
    isp_sns_cfg.bHwSync = pstViInfo->stSnsInfo.u8HwSync;
    isp_sns_cfg.S32MipiDevno = pstViInfo->stSnsInfo.MipiDev;
    isp_sns_cfg.bMclkEn = 1;
    #ifndef DUAL_OS
    isp_sns_cfg.I2cAddr = pstViInfo->stSnsInfo.s32SnsI2cAddr;
    memset(&isp_sns_cfg.busInfo, 0, sizeof(ISP_SNS_COMMBUS_U));
    isp_sns_cfg.busInfo.s8I2cDev = pstViInfo->stSnsInfo.s32BusId;
    #endif
    isp_sns_cfg.u8Mclk = pstViInfo->stSnsInfo.u8CamClkId;
    isp_sns_cfg.lane_id[0] = pstViInfo->stSnsInfo.as16LaneId[0];
    isp_sns_cfg.lane_id[1] = pstViInfo->stSnsInfo.as16LaneId[1];
    isp_sns_cfg.lane_id[2] = pstViInfo->stSnsInfo.as16LaneId[2];
    isp_sns_cfg.lane_id[3] = pstViInfo->stSnsInfo.as16LaneId[3];
    isp_sns_cfg.lane_id[4] = pstViInfo->stSnsInfo.as16LaneId[4];
    isp_sns_cfg.pn_swap[0]  = pstViInfo->stSnsInfo.as8PNSwap[0];
    isp_sns_cfg.pn_swap[1]  = pstViInfo->stSnsInfo.as8PNSwap[1];
    isp_sns_cfg.pn_swap[2]  = pstViInfo->stSnsInfo.as8PNSwap[2];
    isp_sns_cfg.pn_swap[3]  = pstViInfo->stSnsInfo.as8PNSwap[3];
    isp_sns_cfg.pn_swap[4]  = pstViInfo->stSnsInfo.as8PNSwap[4];

    #ifdef DUAL_OS
    if (stViPipeAttr.bYuvBypassPath == CVI_FALSE) {

        SNS_I2C_GPIO_INFO_S sns_cfg;

        sns_cfg.s32I2cAddr = pstViInfo->stSnsInfo.s32SnsI2cAddr;
        sns_cfg.s8I2cDev = pstViInfo->stSnsInfo.s32BusId;
        sns_cfg.u32Rst_port_idx = pstViInfo->stSnsInfo.u8RstGpioInx;
        sns_cfg.u32Rst_pin = pstViInfo->stSnsInfo.u8RstGpioPin;
        sns_cfg.u32Rst_pol = pstViInfo->stSnsInfo.u8RstGpioPol;

        CVI_LOGD("sns_cfg I2cAddr %d I2cDev %d idx %u pin %u pol %u", sns_cfg.s32I2cAddr,
                sns_cfg.s8I2cDev, sns_cfg.u32Rst_port_idx, sns_cfg.u32Rst_pin, sns_cfg.u32Rst_pol);
        CVI_SENSOR_GPIO_Init(sns_id, &sns_cfg);
    }


    if (CVI_ISP_SnsInit(sns_id, &isp_sns_cfg) != CVI_SUCCESS) {
        CVI_LOGE("ISP_SnsInit failed\n");
        return CVI_FAILURE;
    }
    #else
    struct snsr_rst_gpio_s sns_gpio;
    sns_gpio.snsr_rst_pin = pstViInfo->stSnsInfo.u8RstGpioPin;
    sns_gpio.snsr_rst_pol = pstViInfo->stSnsInfo.u8RstGpioPol;
    s32Ret = CVI_MIPI_SensorGpioInit(sns_id, (void*)&sns_gpio);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_MIPI_SensorGpioInit failed\n");
        return CVI_FAILURE;
    }

    ISP_SNS_OBJ_S *pstSnsObj = _MAPI_VCAP_Get_SnsObj(pstViInfo->stSnsInfo.enSnsType);

    if (CVI_ISP_SnsInit(sns_id, &isp_sns_cfg, pstSnsObj, -1) != CVI_SUCCESS) {
        CVI_LOGE("ISP_SnsInit failed\n");
        return CVI_FAILURE;
    }
    #endif

    return CVI_SUCCESS;
}
#endif

static int _MAPI_VCAP_Init(MAPI_VCAP_HANDLE_T *vcap_hdl, MAPI_VCAP_ATTR_T *vcap_attr)
{
    int sns_num = 0;

    sns_num = vcap_attr->u8DevNum;
    if ((sns_num == 0) || (sns_num > VI_MAX_DEV_NUM)) {
        CVI_LOGE("sensor dev number (%d) error\n", sns_num);
        return MAPI_ERR_INVALID;
    }

    MAPI_VCAP_CTX_T *vt = NULL;
    vt = (MAPI_VCAP_CTX_T *)malloc(sizeof(MAPI_VCAP_CTX_T));
    if (!vt) {
        CVI_LOGE("malloc failed\n");
        return MAPI_ERR_NOMEM;
    }

    memset(vt, 0, sizeof(MAPI_VCAP_CTX_T));
    vt->ViConfig.s32WorkingViNum = sns_num;

    #ifdef CHIP_184X
    vt->stSnsCfg.sns_ini_cfg.devNum = sns_num;
    vt->stSnsCfg.sns_ini_cfg.enSnsMode = 0;
    #endif

    for (int i = 0; i < sns_num; i++) {
        vt->ViConfig.astViInfo[i].stSnsInfo.s32SnsId        = vcap_attr->attr_sns[i].u8SnsId;
        vt->ViConfig.astViInfo[i].stSnsInfo.enSnsType       = vcap_attr->attr_sns[i].u32sensortype;
        CVI_LOGI("enSnsType = %#x\n", vcap_attr->attr_sns[i].u32sensortype);
        vt->ViConfig.astViInfo[i].stSnsInfo.MipiDev         = vcap_attr->attr_sns[i].u8MipiDev;
        vt->ViConfig.astViInfo[i].stSnsInfo.s32BusId        = vcap_attr->attr_sns[i].u8I2cBusId;
        vt->ViConfig.astViInfo[i].stSnsInfo.s32SnsI2cAddr   = vcap_attr->attr_sns[i].u8I2cSlaveAddr;
        vt->ViConfig.astViInfo[i].stSnsInfo.u8HwSync        = vcap_attr->attr_sns[i].u8HwSync;

        for (int j = 0; j < 5; j++) {
            vt->ViConfig.astViInfo[i].stSnsInfo.as16LaneId[j] = vcap_attr->attr_sns[i].as8LaneId[j];
            vt->ViConfig.astViInfo[i].stSnsInfo.as8PNSwap[j]    = vcap_attr->attr_sns[i].as8PNSwap[j];
        }

        vt->ViConfig.astViInfo[i].stSnsInfo.u8CamClkId      = vcap_attr->attr_sns[i].u8CamClkId;
        vt->ViConfig.astViInfo[i].stSnsInfo.u8RstGpioInx    = vcap_attr->attr_sns[i].u8RstGpioInx;
        vt->ViConfig.astViInfo[i].stSnsInfo.u8RstGpioPin    = vcap_attr->attr_sns[i].u8RstGpioPin;
        vt->ViConfig.astViInfo[i].stSnsInfo.u8RstGpioPol    = vcap_attr->attr_sns[i].u8RstGpioPol;

        vt->ViConfig.astViInfo[i].stChnInfo.s32ChnId        = i;
        vt->ViConfig.astViInfo[i].stChnInfo.u32Width        = vcap_attr->attr_chn[i].u32Width;
        vt->ViConfig.astViInfo[i].stChnInfo.u32Height       = vcap_attr->attr_chn[i].u32Height;
        vt->ViConfig.astViInfo[i].stChnInfo.f32Fps          = (vcap_attr->attr_chn[i].f32Fps==0)?25.0f:vcap_attr->attr_chn[i].f32Fps;
        vt->ViConfig.astViInfo[i].stChnInfo.enPixFormat     = vcap_attr->attr_chn[i].enPixelFmt;
        vt->ViConfig.astViInfo[i].stChnInfo.enWDRMode       = vcap_attr->attr_sns[i].u8WdrMode;
        vt->ViConfig.astViInfo[i].stChnInfo.enCompressMode  = vcap_attr->attr_chn[i].enCompressMode;
        vt->ViConfig.astViInfo[i].stChnInfo.vbcnt       = vcap_attr->attr_chn[i].vbcnt;


        CVI_LOGI("Sensor[%d]: type %d, mipi %d, bus %d, s32SnsI2cAddr %d\n",
                i,
                vt->ViConfig.astViInfo[i].stSnsInfo.enSnsType,
                vt->ViConfig.astViInfo[i].stSnsInfo.MipiDev,
                vt->ViConfig.astViInfo[i].stSnsInfo.s32BusId,
                vt->ViConfig.astViInfo[i].stSnsInfo.s32SnsI2cAddr);

        #ifdef CHIP_184X
        for (int j = 0; j < VI_MAX_PIPE_NUM; j++) {
            vt->ViConfig.astViInfo[i].stSnsInfo.aPipe[j] = vcap_attr->attr_sns[i].aPipe[j];
        }
        vt->stSnsCfg.sns_ini_cfg.enSnsType[i] = vcap_attr->attr_sns[i].u32sensortype;
        vt->stSnsCfg.sns_ini_cfg.s32BusId[i] = vcap_attr->attr_sns[i].u8I2cBusId;
        vt->stSnsCfg.sns_ini_cfg.s32SnsI2cAddr[i] = vcap_attr->attr_sns[i].u8I2cSlaveAddr;
        vt->stSnsCfg.sns_ini_cfg.MipiDev[i] = vcap_attr->attr_sns[i].u8MipiDev;

        for (int j = 0; j < 5; j++) {
            vt->stSnsCfg.sns_ini_cfg.as16LaneId[i][j] = vcap_attr->attr_sns[i].as8LaneId[j];
            vt->stSnsCfg.sns_ini_cfg.as8PNSwap[i][j] = vcap_attr->attr_sns[i].as8PNSwap[j];
        }

        for (int j = 0; j < TTL_PIN_FUNC_NUM; j++) {
            vt->stSnsCfg.sns_ini_cfg.as16FuncId[i][j] = vcap_attr->attr_sns[i].as8FuncId[j];
        }

        vt->stSnsCfg.sns_ini_cfg.u8HwSync[i] = vcap_attr->attr_sns[i].u8HwSync;
        vt->stSnsCfg.sns_ini_cfg.stMclkAttr[i].bMclkEn = vcap_attr->attr_sns[i].bMclkEn;
        vt->stSnsCfg.sns_ini_cfg.stMclkAttr[i].u8Mclk  = vcap_attr->attr_sns[i].u8CamClkId;
        vt->stSnsCfg.sns_ini_cfg.s32RstPort[i] = vcap_attr->attr_sns[i].u8RstGpioInx;
        vt->stSnsCfg.sns_ini_cfg.s32RstPin[i] = vcap_attr->attr_sns[i].u8RstGpioPin;
        vt->stSnsCfg.sns_ini_cfg.s32RstPol[i] = vcap_attr->attr_sns[i].u8RstGpioPol;
        vt->stSnsCfg.sns_ini_cfg.bHsettlen[i] = vcap_attr->attr_sns[i].bHsettlen;
        vt->stSnsCfg.sns_ini_cfg.u8Hsettle[i] = vcap_attr->attr_sns[i].u8Hsettle;
        vt->stSnsCfg.sns_ini_cfg.u8Orien[i] = vcap_attr->attr_sns[i].u8Orien;
        #endif

    }

#ifndef DUAL_OS
    if (CVI_SYS_VI_Open() != CVI_SUCCESS) {
        CVI_LOGE("CVI_SYS_VI_Open failed\n");
        goto error;
    }
#endif

#ifndef CHIP_184X
    for (int i = 0; i < sns_num; i++) {
        if (_MAPI_VCAP_StartSensor(&vt->ViConfig.astViInfo[i], i) != CVI_SUCCESS) {
            CVI_LOGE("Start Sensor failed\n");
            goto error;
        }
    }
#endif

#ifdef CHIP_184X
    if (CVI_SNS_GetConfigInfo(&vt->stSnsCfg) != MAPI_SUCCESS) {
        CVI_LOGE("CVI_SNS_GetConfigInfo failed\n");
        goto error;
    }
    if (CVI_SNS_SetSnsDrvCfg(&vt->stSnsCfg) != MAPI_SUCCESS) {
        CVI_LOGE("CVI_SNS_SetSnsDrvCfg failed\n");
        goto error;
    }
#endif

    *vcap_hdl = (MAPI_VCAP_HANDLE_T *)vt;
    return MAPI_SUCCESS;
error:
    if (vt != NULL) {
        free(vt);
        vt = NULL;
    }
    return MAPI_ERR_FAILURE;
}

static int _MAPI_VCAP_Deinit(MAPI_VCAP_HANDLE_T vcap_hdl)
{
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)vcap_hdl;

    if (vt == NULL) {
        return MAPI_SUCCESS;
    }

    if (vt != NULL) {
        free(vt);
        vt = NULL;
    }

#ifndef DUAL_OS
    if (CVI_SYS_VI_Close() != MAPI_SUCCESS) {
        CVI_LOGE("CVI_SYS_VI_Close failed\n");
        return MAPI_ERR_FAILURE;
    }
#endif

    return MAPI_SUCCESS;
}

int MAPI_VCAP_InitSensor(MAPI_VCAP_SENSOR_HANDLE_T *sns_hdl, int sns_id, MAPI_VCAP_ATTR_T *vcap_attr)
{
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);
    CHECK_MAPI_VCAP_NULL_PTR_RET(vcap_attr);

    pthread_mutex_lock(&vcap_mutex);

    if (vcap_ctx == NULL) {
        if (_MAPI_VCAP_Init(&vcap_ctx, vcap_attr) != MAPI_SUCCESS) {
            CVI_LOGE("_MAPI_VCAP_Init fail\n");
            pthread_mutex_unlock(&vcap_mutex);
            return MAPI_ERR_FAILURE;
        }
    }

    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)vcap_ctx;

    MAPI_VCAP_SENSOR_T *st = NULL;
    st = (MAPI_VCAP_SENSOR_T *)malloc(sizeof(MAPI_VCAP_SENSOR_T));
    if (!st) {
        CVI_LOGE("malloc failed\n");
        _MAPI_VCAP_Deinit(vcap_ctx);
        pthread_mutex_unlock(&vcap_mutex);
        return MAPI_ERR_NOMEM;
    }
    memset(st, 0, sizeof(MAPI_VCAP_SENSOR_T));
    st->sns_id = sns_id;
    st->attr = *vcap_attr;
    st->vcap_hdl = vcap_ctx;

    vt->ref_count ++;
    pthread_mutex_unlock(&vcap_mutex);

    *sns_hdl = (MAPI_VCAP_SENSOR_HANDLE_T)st;
    return MAPI_SUCCESS;
}

int MAPI_VCAP_DeinitSensor(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, st->sns_id);

    pthread_mutex_lock(&vcap_mutex);

    if (vcap_ctx == NULL) {
        CVI_LOGE("VCAP not initialized\n");
        pthread_mutex_unlock(&vcap_mutex);
        return MAPI_ERR_INVALID;
    }
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)vcap_ctx;
    vt->ref_count --;
    CVI_LOG_ASSERT(vt->ref_count >= 0, "VCAP CTX ref_count < 0\n");

    if (vt->ref_count == 0) {
        _MAPI_VCAP_Deinit(vcap_ctx);
        vcap_ctx = NULL;
    }

    pthread_mutex_unlock(&vcap_mutex);

    if (st != NULL) {
        free(st);
        st = NULL;
    }

    return MAPI_SUCCESS;
}

#ifdef CHIP_184X
int MAPI_VCAP_StartMipi(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    SNS_COMBO_DEV_ATTR_S combo_dev_attr;
    CVI_S32 devno = 0, rstport, rstpin, rstpol;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    devno = pstViInfo->stSnsInfo.MipiDev;
    rstport = pstViInfo->stSnsInfo.u8RstGpioInx;
    rstpin = pstViInfo->stSnsInfo.u8RstGpioPin;
    rstpol = pstViInfo->stSnsInfo.u8RstGpioPol;

    s32Ret = CVI_SNS_GetSnsRxAttr(sns_id, &combo_dev_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "get mipi dev_%d attr failed!\n", sns_id);

    s32Ret = CVI_MIPI_SetSensorReset(devno, rstport, rstpin, rstpol, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorReset failed");

    s32Ret = CVI_MIPI_SetMipiReset(sns_id, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetMipiReset failed");

    s32Ret = CVI_MIPI_SetMipiAttr(sns_id, (CVI_VOID*)&combo_dev_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetMipiAttr failed");

    s32Ret = CVI_MIPI_SetSensorClock(sns_id, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorClock failed");

    s32Ret = CVI_MIPI_SetSensorReset(devno, rstport, rstpin, rstpol, 0);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorReset failed");

    s32Ret = CVI_SNS_SetSnsProbe(sns_id);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SNS_SetSnsProbe failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_MipiReset(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    CVI_S32 devno = 0, rstport, rstpin, rstpol;
    // MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    // pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    // devno = pstViInfo->stSnsInfo.MipiDev;
    // rstport = pstViInfo->stSnsInfo.u8RstGpioInx;
    // rstpin = pstViInfo->stSnsInfo.u8RstGpioPin;
    // rstpol = pstViInfo->stSnsInfo.u8RstGpioPol;
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;
    devno = pstSnsCfg->sns_ini_cfg.MipiDev[sns_id];
    rstport = pstSnsCfg->sns_ini_cfg.s32RstPort[sns_id];
    rstpin = pstSnsCfg->sns_ini_cfg.s32RstPin[sns_id];
    rstpol = pstSnsCfg->sns_ini_cfg.s32RstPol[sns_id];

    s32Ret = CVI_MIPI_SetSensorReset(devno, rstport, rstpin, rstpol, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorReset failed");

    s32Ret = CVI_MIPI_SetMipiReset(sns_id, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetMipiReset failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetMipiAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    SNS_COMBO_DEV_ATTR_S combo_dev_attr;

    s32Ret = CVI_SNS_GetSnsRxAttr(sns_id, &combo_dev_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "get mipi dev_%d attr failed!\n", sns_id);

    s32Ret = CVI_MIPI_SetMipiAttr(sns_id, (CVI_VOID*)&combo_dev_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetMipiAttr failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetSensorClock(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    s32Ret = CVI_MIPI_SetSensorClock(sns_id, 1);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorClock failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetSensorReset(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    CVI_S32 devno = 0, rstport, rstpin, rstpol;
    // MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    // pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    // devno = pstViInfo->stSnsInfo.MipiDev;
    // rstport = pstViInfo->stSnsInfo.u8RstGpioInx;
    // rstpin = pstViInfo->stSnsInfo.u8RstGpioPin;
    // rstpol = pstViInfo->stSnsInfo.u8RstGpioPol;
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;
    devno = pstSnsCfg->sns_ini_cfg.MipiDev[sns_id];
    rstport = pstSnsCfg->sns_ini_cfg.s32RstPort[sns_id];
    rstpin = pstSnsCfg->sns_ini_cfg.s32RstPin[sns_id];
    rstpol = pstSnsCfg->sns_ini_cfg.s32RstPol[sns_id];

    s32Ret = CVI_MIPI_SetSensorReset(devno, rstport, rstpin, rstpol, 0);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_MIPI_SetSensorReset failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetSnsProbe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    s32Ret = CVI_SNS_SetSnsProbe(sns_id);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SNS_SetSnsProbe failed");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetSnsInit(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    s32Ret = CVI_SNS_SetSnsInit(sns_id);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SNS_SetSnsInit(%d) failed!\n", sns_id);

    return MAPI_SUCCESS;
}
#endif

int MAPI_VCAP_StartDev(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_DEV         ViDev;
    VI_DEV_ATTR_S  stViDevAttr;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViDev = pstViInfo->stChnInfo.s32ChnId;

    memset(&stViDevAttr, 0x0, sizeof(stViDevAttr));
    #ifdef __CV184X__
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;
    stViDevAttr.snrFps				= pstSnsCfg->sns_cfg.f32FrameRate[ViDev];
    stViDevAttr.stSize.u32Width		= pstSnsCfg->sns_cfg.u32ImageWigth[ViDev];
    stViDevAttr.stSize.u32Height	= pstSnsCfg->sns_cfg.u32ImageHeight[ViDev];
    stViDevAttr.enIntfMode			= (VI_INTF_MODE_E)pstSnsCfg->sns_cfg.enInterFaceMode[ViDev];
    stViDevAttr.enInputDataType		= (VI_DATA_TYPE_E)pstSnsCfg->sns_cfg.enFormatMode[ViDev];
    stViDevAttr.enDataSeq			= (VI_YUV_DATA_SEQ_E)pstSnsCfg->sns_cfg.enYuvFormat[ViDev];
    stViDevAttr.stWDRAttr.enWDRMode	= pstSnsCfg->sns_cfg.enWDRMode[ViDev];
    stViDevAttr.enWorkMode			= (VI_WORK_MODE_E)pstSnsCfg->sns_cfg.enChnMode[ViDev];
    stViDevAttr.enBayerFormat			= (VI_WORK_MODE_E)pstSnsCfg->sns_cfg.enBayerFormat[ViDev];
    stViDevAttr.enScanMode			= VI_SCAN_PROGRESSIVE;
    stViDevAttr.enYuvSceneMode		= (VI_ISP_YUV_SCENE_E)st->attr.attr_chn[ViDev].scenemode;

    #else
    ISP_PUB_ATTR_S stPubAttr;
    stViDevAttr.stWDRAttr.enWDRMode = pstViInfo->stChnInfo.enWDRMode;
    s32Ret = MAPI_VCAP_GetDevAttr(pstViInfo->stSnsInfo.enSnsType, &stViDevAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetDevAttr(%d) failed!\n", ViDev);

    memset(&stPubAttr, 0x0, sizeof(stPubAttr));
    s32Ret = MAPI_VCAP_GetIspPubAttr(pstViInfo->stSnsInfo.enSnsType, &stPubAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetIspPubAttr(%d) failed!\n", ViDev);

    stViDevAttr.stSize.u32Width     = pstViInfo->stChnInfo.u32Width;
    stViDevAttr.stSize.u32Height    = pstViInfo->stChnInfo.u32Height;
    stViDevAttr.enBayerFormat = (BAYER_FORMAT_E)stPubAttr.enBayer;
    stViDevAttr.snrFps = pstViInfo->stChnInfo.f32Fps;
    #endif

    #ifdef CHIP_184X
    VI_DEV_BIND_PIPE_S stViDevBindAttr;
    stViDevBindAttr.PipeId[0]		= pstSnsCfg->sns_ini_cfg.MipiDev[ViDev];
    stViDevBindAttr.u32Num			= 1;
    stViDevBindAttr.MipiDev = pstSnsCfg->sns_ini_cfg.MipiDev[ViDev];
    s32Ret = CVI_VI_SetDevBindAttr(ViDev, &stViDevBindAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_VI_SetDevBindAttr(%d) failed!\n", ViDev);
    #endif

    s32Ret = CVI_VI_SetDevAttr(ViDev, &stViDevAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_SetDevAttr(%d) failed!\n", ViDev);

    s32Ret = CVI_VI_EnableDev(ViDev);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_EnableDev(%d) failed!\n", ViDev);

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StopDev(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_DEV         ViDev;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViDev = pstViInfo->stChnInfo.s32ChnId;

    if (CVI_VI_DisableDev(ViDev) != CVI_SUCCESS) {
        CVI_LOGE("VI_DisableDev failed with %d\n", ViDev);
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StartChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    VI_CHN_ATTR_S  stViChnAttr;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    memset(&stViChnAttr, 0, sizeof(VI_CHN_ATTR_S));
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;
    stViChnAttr.stSize.u32Width = pstSnsCfg->sns_cfg.u32ImageWigth[ViPipe];
    stViChnAttr.stSize.u32Height = pstSnsCfg->sns_cfg.u32ImageHeight[ViPipe];
    stViChnAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;//DYNAMIC_RANGE_SDR8
    stViChnAttr.enVideoFormat  = VIDEO_FORMAT_LINEAR;//VIDEO_FORMAT_LINEAR
    stViChnAttr.u32BindVbPool = -1;
    stViChnAttr.bSingleVb = false;

    /* fill the sensor orientation */
    stViChnAttr.bMirror = false;
    stViChnAttr.bFlip = false;
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;
    s32Ret = MAPI_VCAP_GetChnAttr(pstViInfo->stSnsInfo.enSnsType, &stViChnAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetChnAttr(%d) failed!\n", ViPipe);

    stViChnAttr.stSize.u32Width  = pstViInfo->stChnInfo.u32Width;
    stViChnAttr.stSize.u32Height = pstViInfo->stChnInfo.u32Height;
    #endif

    stViChnAttr.u32Depth         = 1; // depth
    stViChnAttr.enPixelFormat = pstViInfo->stChnInfo.enPixFormat;
    stViChnAttr.enCompressMode = pstViInfo->stChnInfo.enCompressMode;

    s32Ret = CVI_VI_SetChnAttr(ViPipe, ViChn, &stViChnAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_SetChnAttr(%d) failed!\n", ViPipe);

#ifdef DUAL_OS
    #ifdef CHIP_184X
    s32Ret = CVI_SNS_SetVIFlipMirrorCB(ViPipe, sns_id);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SNS_SetVIFlipMirrorCB(%d) failed!\n", ViPipe);
    #else
    s32Ret = CVI_SENSOR_SetVIFlipMirrorCB(ViPipe, ViChn);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SENSOR_SetVIFlipMirrorCB(%d, %d) failed!\n", ViPipe, ViChn);
    #endif
#else
    //待处理
    // s32Ret = CVI_VI_RegChnFlipMirrorCallBack(ViPipe, ViChn, (void *)pstSnsObj->pfnMirrorFlip);
#endif

    if(pstViInfo->stChnInfo.vbcnt > 0) {
        VI_VPSS_MODE_S stVIVPSSMode;
        CVI_SYS_GetVIVPSSMode(&stVIVPSSMode);

        if(stVIVPSSMode.aenMode[sns_id] == VI_OFFLINE_VPSS_OFFLINE) {
            VB_POOL_CONFIG_S stVbPoolCfg;
            VB_POOL chnVbPool;
            CVI_U32 u32BlkSize = 0;

            u32BlkSize = COMMON_GetPicBufferSize(pstViInfo->stChnInfo.u32Width, pstViInfo->stChnInfo.u32Height ,pstViInfo->stChnInfo.enPixFormat ,
                DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);

            memset(&stVbPoolCfg, 0, sizeof(VB_POOL_CONFIG_S));
            stVbPoolCfg.u32BlkSize	= u32BlkSize;
            stVbPoolCfg.u32BlkCnt	= pstViInfo->stChnInfo.vbcnt;
            stVbPoolCfg.enRemapMode = VB_REMAP_MODE_CACHED;
            chnVbPool = CVI_VB_CreatePool(&stVbPoolCfg);
            if (chnVbPool == VB_INVALID_POOLID) {
                CVI_LOGE("VB_CreatePool failed.\n");
            } else {
                CVI_VI_AttachVbPool(ViPipe,ViChn,chnVbPool);
                pstViInfo->stChnInfo.bindVbPool = chnVbPool;
            }
        }
    }

    s32Ret = CVI_VI_EnableChn(ViPipe, ViChn);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_EnableChn(%d) failed!\n", ViPipe);

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StopChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    CVI_VI_UnRegChnFlipMirrorCallBack(ViPipe, ViChn);
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;
    CVI_VI_UnRegChnFlipMirrorCallBack(ViPipe, ViChn);
    #endif

    s32Ret = CVI_VI_DisableChn(ViPipe, ViChn);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_DisableChn(%d) failed!\n", ViPipe);

    if(pstViInfo->stChnInfo.vbcnt > 0 && pstViInfo->stChnInfo.bindVbPool != VB_INVALID_POOLID){
        CVI_LOGI("[VCAP][ViPipe:%d][ViChn:%d] attach vb : %d \n",ViPipe, ViChn, pstViInfo->stChnInfo.bindVbPool);
        CVI_VB_DestroyPool(pstViInfo->stChnInfo.bindVbPool);
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StartPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE        ViPipe;
    VI_PIPE_ATTR_S stViPipeAttr;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    #ifdef CHIP_184X
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    stViPipeAttr.u32MaxW						= pstSnsCfg->sns_cfg.u32ImageWigth[ViPipe];
    stViPipeAttr.u32MaxH						= pstSnsCfg->sns_cfg.u32ImageHeight[ViPipe];
    stViPipeAttr.enPixFmt						= PIXEL_FORMAT_RGB_BAYER_12BPP;
    stViPipeAttr.enBitWidth					= DATA_BITWIDTH_12;
    stViPipeAttr.stFrameRate.s32SrcFrameRate	= -1;
    stViPipeAttr.stFrameRate.s32DstFrameRate	= -1;
    stViPipeAttr.bNrEn						= CVI_TRUE;
    stViPipeAttr.bYuvBypassPath				= pstSnsCfg->sns_cfg.bBypassIsp[ViPipe];
    stViPipeAttr.enCompressMode = pstViInfo->stChnInfo.enCompressMode;

    for (int j = 0; j < VI_MAX_PIPE_NUM; j++) {
        if ((pstViInfo->stSnsInfo.aPipe[j] >= 0) && (pstViInfo->stSnsInfo.aPipe[j] < VI_MAX_PIPE_NUM)) {
            ViPipe = pstViInfo->stSnsInfo.aPipe[j];
            s32Ret = CVI_VI_CreatePipe(ViPipe, &stViPipeAttr);
            CHECK_MAPI_VCAP_RET(s32Ret, "CVI_VI_CreatePipe(%d) failed!\n", ViPipe);
            s32Ret = CVI_VI_StartPipe(ViPipe);
            CHECK_MAPI_VCAP_RET(s32Ret, "CVI_VI_StartPipe(%d) failed!\n", ViPipe);
        }
    }
    #else
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr(%d) failed!\n", ViPipe);

    stViPipeAttr.u32MaxW = pstViInfo->stChnInfo.u32Width;
    stViPipeAttr.u32MaxH = pstViInfo->stChnInfo.u32Height;
    stViPipeAttr.enCompressMode = pstViInfo->stChnInfo.enCompressMode;
    s32Ret = CVI_VI_CreatePipe(ViPipe, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_CreatePipe(%d) failed!\n", ViPipe);

    s32Ret = CVI_VI_StartPipe(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_StartPipe(%d) failed!\n", ViPipe);
    #endif

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StopPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    s32Ret = CVI_VI_StopPipe(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_StopPipe(%d) failed!\n", ViPipe);

    s32Ret = CVI_VI_DestroyPipe(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "VI_DestroyPipe(%d) failed!\n", ViPipe);

    return MAPI_SUCCESS;
}

static ISP_EXPOSURE_ATTR_S stgExpAttr;
static ISP_AWB_ATTR_EX_S stgAWBAttrEx;
static ISP_CA_ATTR_S stgCAAttr;
static ISP_PRESHARPEN_EDGE_EXT_ATTR_S stgEdgeExtAttr;
static ISP_SHARPEN_ATTR_S stgSharp;

int MAPI_VCAP_SetISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE              ViPipe;
    MAPI_VI_INFO_S       *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_TRUE) {
        CVI_LOGW("yuv sensor skip isp init\n");
        return CVI_SUCCESS;
    }

    if (stgExpAttr.stAuto.stISONumRange.u32Max > 0) {
        if (stgExpAttr.enOpType) {
            stgExpAttr.enOpType = OP_TYPE_AUTO;
        }

        s32Ret = CVI_ISP_SetExposureAttr(ViPipe, &stgExpAttr);
        if (s32Ret != 0) {
            CVI_LOGW("CVI_ISP_SetExposureAttr failed with error code %d\n", s32Ret);
        }

        s32Ret = CVI_ISP_SetAWBAttrEx(ViPipe, &stgAWBAttrEx);
        if (s32Ret != 0) {
            CVI_LOGW("CVI_ISP_SetAWBAttrEx failed with error code %d\n", s32Ret);
        }

        s32Ret = CVI_ISP_SetCAAttr(ViPipe, &stgCAAttr);
        if (s32Ret != 0) {
            CVI_LOGW("CVI_ISP_SetCAAttr failed with error code %d\n", s32Ret);
        }

        s32Ret = CVI_ISP_SetPreSharpenEdgeExtAttr(ViPipe, &stgEdgeExtAttr);
        if (s32Ret != 0) {
            CVI_LOGW("CVI_ISP_SetPreSharpenEdgeExtAttr failed with error code %d\n", s32Ret);
        }

        s32Ret = CVI_ISP_SetSharpenAttr(ViPipe, &stgSharp);
        if (s32Ret != 0) {
            CVI_LOGW("CVI_ISP_SetSharpenAttr failed with error code %d\n", s32Ret);
        }
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);
    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    #ifdef CHIP_184X
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_TRUE) {
        return MAPI_SUCCESS;
    }
    #else
    VI_PIPE_ATTR_S stViPipeAttr;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    if (stViPipeAttr.bYuvBypassPath == CVI_TRUE) {
        return MAPI_SUCCESS;
    }
    #endif

    s32Ret = CVI_ISP_GetExposureAttr(ViPipe, &stgExpAttr);
    if (s32Ret != 0) {
        CVI_LOGW("CVI_ISP_GetExposureAttr failed with error code %d\n", s32Ret);
    }

    s32Ret = CVI_ISP_GetAWBAttrEx(ViPipe, &stgAWBAttrEx);
    if (s32Ret != 0) {
        CVI_LOGW("CVI_ISP_GetAWBAttrEx failed with error code %d\n", s32Ret);
    }

    s32Ret = CVI_ISP_GetCAAttr(ViPipe, &stgCAAttr);
    if (s32Ret != 0) {
        CVI_LOGW("CVI_ISP_GetCAAttr failed with error code %d\n", s32Ret);
    }

    s32Ret = CVI_ISP_GetPreSharpenEdgeExtAttr(ViPipe, &stgEdgeExtAttr);
    if (s32Ret != 0) {
        CVI_LOGW("CVI_ISP_GetPreSharpenEdgeExtAttr failed with error code %d\n", s32Ret);
    }

    s32Ret = CVI_ISP_GetSharpenAttr(ViPipe, &stgSharp);
    if (s32Ret != 0) {
        CVI_LOGW("CVI_ISP_GetSharpenAttr failed with error code %d\n", s32Ret);
    }

    return s32Ret;
}

#ifndef CHIP_184X
int MAPI_VCAP_InitISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE              ViPipe;
    MAPI_VI_INFO_S       *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    VI_PIPE_ATTR_S stViPipeAttr;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr(%d) failed!\n", ViPipe);

    if (stViPipeAttr.bYuvBypassPath == CVI_TRUE) {
        CVI_LOGW("yuv sensor skip isp init\n");
        return CVI_SUCCESS;
    }

    #ifdef DUAL_OS
    s32Ret = CVI_ISP_SetBypassFrm(ViPipe, MAPI_ISP_BYPASS_CNT);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP Set bypass fail, ViPipe[%d]\n", ViPipe);

    #else
    ISP_PUB_ATTR_S stPubAttr;
    s32Ret = MAPI_VCAP_GetIspPubAttr(pstViInfo->stSnsInfo.enSnsType, &stPubAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP MAPI_VCAP_GetIspPubAttr fail, ViPipe[%d]\n", ViPipe);

    stPubAttr.stWndRect.s32X = 0;
    stPubAttr.stWndRect.s32Y = 0;
    stPubAttr.stWndRect.u32Width  = pstViInfo->stChnInfo.u32Width;
    stPubAttr.stWndRect.u32Height = pstViInfo->stChnInfo.u32Height;
    stPubAttr.stSnsSize.u32Width  = pstViInfo->stChnInfo.u32Width;
    stPubAttr.stSnsSize.u32Height = pstViInfo->stChnInfo.u32Height;
    stPubAttr.f32FrameRate        = pstViInfo->stChnInfo.f32Fps;
    stPubAttr.enWDRMode           = pstViInfo->stChnInfo.enWDRMode;
    s32Ret = CVI_ISP_SetPubAttr(ViPipe, &stPubAttr);

     ALG_LIB_S            stAeLib;
    ALG_LIB_S            stAwbLib;
    ISP_BIND_ATTR_S      stBindAttr;
    ISP_STATISTICS_CFG_S stsCfg;
    stAeLib.s32Id = ViPipe;
    strcpy(stAeLib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
    s32Ret = CVI_AE_Register(ViPipe, &stAeLib);
    CHECK_MAPI_VCAP_RET(s32Ret, "AE Algo register fail, ViPipe[%d]\n", ViPipe);

    stAwbLib.s32Id = ViPipe;
    strcpy(stAwbLib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));
    s32Ret = CVI_AWB_Register(ViPipe, &stAwbLib);
    CHECK_MAPI_VCAP_RET(s32Ret, "AWB Algo register fail, ViPipe[%d]\n", ViPipe);

    memset(&stBindAttr, 0, sizeof(ISP_BIND_ATTR_S));
    stBindAttr.sensorId = 0;
    snprintf(stBindAttr.stAeLib.acLibName, sizeof(CVI_AE_LIB_NAME), "%s", CVI_AE_LIB_NAME);
    stBindAttr.stAeLib.s32Id = ViPipe;
    snprintf(stBindAttr.stAwbLib.acLibName, sizeof(CVI_AWB_LIB_NAME), "%s", CVI_AWB_LIB_NAME);
    stBindAttr.stAwbLib.s32Id = ViPipe;
    s32Ret = CVI_ISP_SetBindAttr(ViPipe, &stBindAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "Bind Algo fail, ViPipe[%d]\n", ViPipe);

    s32Ret = CVI_ISP_MemInit(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "Init Ext memory fail, ViPipe[%d]\n", ViPipe);

    memset(&stsCfg, 0, sizeof(ISP_STATISTICS_CFG_S));
    s32Ret = CVI_ISP_GetStatisticsConfig(ViPipe, &stsCfg);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP Get Statistic fail, ViPipe[%d]\n", ViPipe);
    stsCfg.stAECfg.stCrop[0].bEnable = 0;
    stsCfg.stAECfg.stCrop[0].u16X = 0;
    stsCfg.stAECfg.stCrop[0].u16Y = 0;
    stsCfg.stAECfg.stCrop[0].u16W = stPubAttr.stWndRect.u32Width;
    stsCfg.stAECfg.stCrop[0].u16H = stPubAttr.stWndRect.u32Height;
    memset(stsCfg.stAECfg.au8Weight, 1,
            AE_WEIGHT_ZONE_ROW * AE_WEIGHT_ZONE_COLUMN * sizeof(CVI_U8));

    // stsCfg.stAECfg.stCrop[1].bEnable = 0;
    // stsCfg.stAECfg.stCrop[1].u16X = 0;
    // stsCfg.stAECfg.stCrop[1].u16Y = 0;
    // stsCfg.stAECfg.stCrop[1].u16W = stPubAttr.stWndRect.u32Width;
    // stsCfg.stAECfg.stCrop[1].u16H = stPubAttr.stWndRect.u32Height;

    stsCfg.stWBCfg.u16ZoneRow = AWB_ZONE_ORIG_ROW;
    stsCfg.stWBCfg.u16ZoneCol = AWB_ZONE_ORIG_COLUMN;
    stsCfg.stWBCfg.stCrop.u16X = 0;
    stsCfg.stWBCfg.stCrop.u16Y = 0;
    stsCfg.stWBCfg.stCrop.u16W = stPubAttr.stWndRect.u32Width;
    stsCfg.stWBCfg.stCrop.u16H = stPubAttr.stWndRect.u32Height;
    stsCfg.stWBCfg.u16BlackLevel = 0;
    stsCfg.stWBCfg.u16WhiteLevel = 4095;
    stsCfg.stFocusCfg.stConfig.bEnable = 1;
    stsCfg.stFocusCfg.stConfig.u8HFltShift = 1;
    stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[0] = 1;
    stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[1] = 2;
    stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[2] = 3;
    stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[3] = 5;
    stsCfg.stFocusCfg.stConfig.s8HVFltLpCoeff[4] = 10;
    stsCfg.stFocusCfg.stConfig.stRawCfg.PreGammaEn = 0;
    stsCfg.stFocusCfg.stConfig.stPreFltCfg.PreFltEn = 1;
    stsCfg.stFocusCfg.stConfig.u16Hwnd = 17;
    stsCfg.stFocusCfg.stConfig.u16Vwnd = 15;
    stsCfg.stFocusCfg.stConfig.stCrop.bEnable = 0;
    // AF offset and size has some limitation.
    stsCfg.stFocusCfg.stConfig.stCrop.u16X = AF_XOFFSET_MIN;
    stsCfg.stFocusCfg.stConfig.stCrop.u16Y = AF_YOFFSET_MIN;
    stsCfg.stFocusCfg.stConfig.stCrop.u16W = stPubAttr.stWndRect.u32Width - AF_XOFFSET_MIN * 2;
    stsCfg.stFocusCfg.stConfig.stCrop.u16H = stPubAttr.stWndRect.u32Height - AF_YOFFSET_MIN * 2;
    //Horizontal HP0
    stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[0] = 0;
    stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[1] = 0;
    stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[2] = 13;
    stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[3] = 24;
    stsCfg.stFocusCfg.stHParam_FIR0.s8HFltHpCoeff[4] = 0;
    //Horizontal HP1
    stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[0] = 1;
    stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[1] = 2;
    stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[2] = 4;
    stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[3] = 8;
    stsCfg.stFocusCfg.stHParam_FIR1.s8HFltHpCoeff[4] = 0;
    //Vertical HP
    stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[0] = 13;
    stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[1] = 24;
    stsCfg.stFocusCfg.stVParam_FIR.s8VFltHpCoeff[2] = 0;

    stsCfg.unKey.bit1FEAeGloStat = 1;
    stsCfg.unKey.bit1FEAeLocStat = 1;
    stsCfg.unKey.bit1AwbStat1 = 1;
    stsCfg.unKey.bit1AwbStat2 = 1;
    stsCfg.unKey.bit1FEAfStat = 1;

    //LDG
    stsCfg.stFocusCfg.stConfig.u8ThLow = 0;
    stsCfg.stFocusCfg.stConfig.u8ThHigh = 255;
    stsCfg.stFocusCfg.stConfig.u8GainLow = 30;
    stsCfg.stFocusCfg.stConfig.u8GainHigh = 20;
    stsCfg.stFocusCfg.stConfig.u8SlopLow = 8;
    stsCfg.stFocusCfg.stConfig.u8SlopHigh = 15;

    s32Ret = CVI_ISP_SetStatisticsConfig(ViPipe, &stsCfg);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP Set Statistic fail, ViPipe[%d]\n", ViPipe);
    #endif

    s32Ret = CVI_ISP_Init(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP Init fail, ViPipe[%d]\n", ViPipe);

    MAPI_VCAP_BIN_ReadParaFrombin(s_BinId[sns_id]);
#ifdef ENABLE_ISP_PQ_TOOL
    if (!g_ISPDaemon) {
        isp_daemon2_init(ISPD_CONNECT_PORT);
        g_ISPDaemon = CVI_TRUE;
    }
    #ifdef CHIP_184X
    if (!g_RawDump) {
        cvi_raw_dump_init();
        g_RawDump = CVI_TRUE;
    }
    #endif
#endif

    return MAPI_SUCCESS;
}
#else
int MAPI_VCAP_InitISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    VI_PIPE              ViPipe;
    MAPI_VI_INFO_S       *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_TRUE) {
        CVI_LOGW("yuv sensor skip isp init\n");
        return CVI_SUCCESS;
    }

    ALG_LIB_S            stAeLib;
    ALG_LIB_S            stAwbLib;
    ISP_BIND_ATTR_S      stBindAttr;
    ISP_STATISTICS_CFG_S stsCfg;
    memset(&stAeLib, 0, sizeof(ALG_LIB_S));
    stAeLib.s32Id = ViPipe;
    strcpy(stAeLib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
    s32Ret = CVI_AE_Register(ViPipe, &stAeLib);
    CHECK_MAPI_VCAP_RET(s32Ret, "AE Algo register fail, ViPipe[%d]\n", ViPipe);

    memset(&stAwbLib, 0, sizeof(ALG_LIB_S));
    stAwbLib.s32Id = ViPipe;
    strcpy(stAwbLib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));
    s32Ret = CVI_AWB_Register(ViPipe, &stAwbLib);
    CHECK_MAPI_VCAP_RET(s32Ret, "AWB Algo register fail, ViPipe[%d]\n", ViPipe);

    memset(&stBindAttr, 0, sizeof(ISP_BIND_ATTR_S));
    stBindAttr.sensorId = 0;
    snprintf(stBindAttr.stAeLib.acLibName, sizeof(CVI_AE_LIB_NAME), "%s", CVI_AE_LIB_NAME);
    stBindAttr.stAeLib.s32Id = ViPipe;
    snprintf(stBindAttr.stAwbLib.acLibName, sizeof(CVI_AWB_LIB_NAME), "%s", CVI_AWB_LIB_NAME);
    stBindAttr.stAwbLib.s32Id = ViPipe;
    s32Ret = CVI_ISP_SetBindAttr(ViPipe, &stBindAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "Bind Algo fail, ViPipe[%d]\n", ViPipe);

    s32Ret = CVI_ISP_MemInit(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "Init Ext memory fail, ViPipe[%d]\n", ViPipe);

    ISP_PUB_ATTR_S       stPubAttr;
    memset(&stPubAttr, 0, sizeof(ISP_PUB_ATTR_S));
    stPubAttr.enBayer = (ISP_BAYER_FORMAT_E)pstSnsCfg->sns_cfg.enBayerFormat[ViPipe];
    stPubAttr.stWndRect.s32X = 0;
    stPubAttr.stWndRect.s32Y = 0;
    stPubAttr.stWndRect.u32Width  = pstViInfo->stChnInfo.u32Width;
    stPubAttr.stWndRect.u32Height = pstViInfo->stChnInfo.u32Height;
    stPubAttr.stSnsSize.u32Width  = pstViInfo->stChnInfo.u32Width;
    stPubAttr.stSnsSize.u32Height = pstViInfo->stChnInfo.u32Height;
    // stPubAttr.f32FrameRate        = pstViInfo->stChnInfo.f32Fps;
    // stPubAttr.enWDRMode           = pstViInfo->stChnInfo.enWDRMode;
    stPubAttr.f32FrameRate        = pstSnsCfg->sns_cfg.f32FrameRate[ViPipe];
    stPubAttr.enWDRMode           = pstSnsCfg->sns_cfg.enWDRMode[ViPipe];
    s32Ret = CVI_ISP_SetPubAttr(ViPipe, &stPubAttr);

    s32Ret = CVI_ISP_Init(ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "ISP Init fail, ViPipe[%d]\n", ViPipe);

    MAPI_VCAP_BIN_ReadParaFrombin(s_BinId[sns_id]);
#ifdef ENABLE_ISP_PQ_TOOL
    if (!g_ISPDaemon) {
        isp_daemon2_init(ISPD_CONNECT_PORT);
        g_ISPDaemon = CVI_TRUE;
    }

#endif

    return MAPI_SUCCESS;
}
#endif

int MAPI_VCAP_DeInitISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);
    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    #ifdef CHIP_184X
    s32Ret = CVI_SNS_UnRegCallback(sns_id, ViPipe);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_SNS_UnRegCallback(%d) fail\n", ViPipe);

    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_TRUE) {
        return MAPI_SUCCESS;
    }
    #else
    VI_PIPE_ATTR_S stViPipeAttr;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    if (stViPipeAttr.bYuvBypassPath == CVI_TRUE) {
        return MAPI_SUCCESS;
    }
    #endif

    s32Ret = CVI_ISP_Exit(ViPipe);
    if (s32Ret!= CVI_SUCCESS) {
        printf("CVI_ISP_Exit error id: %d s32Ret %d\n", ViPipe, s32Ret);
    }

    #ifdef CHIP_184X
    ALG_LIB_S           ae_lib;
    ALG_LIB_S           awb_lib;
    ae_lib.s32Id = ViPipe;
    awb_lib.s32Id = ViPipe;

    strcpy(ae_lib.acLibName, CVI_AE_LIB_NAME);//, sizeof(CVI_AE_LIB_NAME));
    strcpy(awb_lib.acLibName, CVI_AWB_LIB_NAME);//, sizeof(CVI_AWB_LIB_NAME));

    s32Ret = CVI_AE_UnRegister(ViPipe, &ae_lib);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_AE_UnRegister(%d) fail\n", ViPipe);

    s32Ret = CVI_AWB_UnRegister(ViPipe, &awb_lib);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_AWB_UnRegister(%d) fail\n", ViPipe);
    #endif


    return s32Ret;
}

int MAPI_VCAP_SetAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_CMD_E enCMD,
                            void *pAttr, uint32_t u32Len)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    CHECK_MAPI_VCAP_NULL_PTR_RET(pAttr);
    CHECK_MAPI_VCAP_MAX_VAL_RET("enCMD", enCMD, (MAPI_VCAP_CMD_BUTT - 1));
    CHECK_MAPI_VCAP_ZERO_VAL_RET("u32Len", u32Len);

    CVI_S32 s32Ret = MAPI_SUCCESS;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    VI_PIPE_ATTR_S stViPipeAttr;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    s32Ret = CVI_VI_GetPipeAttr(ViPipe, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_VI_GetPipeAttr(%d) failed!\n", ViPipe);
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;

    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr(%d) failed!\n", ViPipe);
    #endif

    switch (enCMD) {
    case MAPI_VCAP_CMD_Fps: {
        ISP_PUB_ATTR_S pubAttr;
        CVI_FLOAT *f32FrameRate;
        if (stViPipeAttr.bYuvBypassPath == CVI_TRUE) {
            CVI_LOGW("yuv sensor skip isp ops\n");
            return MAPI_SUCCESS;
        }
        f32FrameRate = (CVI_FLOAT *)pAttr;
        s32Ret = CVI_ISP_GetPubAttr(ViPipe, &pubAttr);
        CHECK_MAPI_VCAP_RET(s32Ret, "call ISP_GetPubAttr fail, ViPipe[%d]\n", ViPipe);

        if (*f32FrameRate == pubAttr.f32FrameRate) {
            return MAPI_SUCCESS;
        }

        pubAttr.f32FrameRate = *f32FrameRate;
        s32Ret = CVI_ISP_SetPubAttr(ViPipe, &pubAttr);
        CHECK_MAPI_VCAP_RET(s32Ret, "call ISP_SetPubAttr fail, ViPipe[%d]\n", ViPipe);
        break;
    }
    case MAPI_VCAP_CMD_Rotate: {
        ROTATION_E enRotation;
        ROTATION_E *penRotationTemp;

        penRotationTemp = (ROTATION_E *)pAttr;
        s32Ret = CVI_VI_GetChnRotation(ViPipe, ViChn, &enRotation);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_GetChnRotation fail, ViChn[%d]\n", ViChn);

        if (*penRotationTemp == enRotation) {
            return MAPI_SUCCESS;
        }

        s32Ret = CVI_VI_SetChnRotation(ViPipe, ViChn, *penRotationTemp);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_SetChnRotation fail, ViChn[%d]\n", ViChn);
        break;
    }
    case MAPI_VCAP_CMD_MirrorFlip: {
        CVI_BOOL tmpFlip, tmpMirror;
        MAPI_VCAP_MIRRORFLIP_ATTR_S *pstMirrorFlip;

        pstMirrorFlip = (MAPI_VCAP_MIRRORFLIP_ATTR_S *)pAttr;
        s32Ret = CVI_VI_GetChnFlipMirror(ViPipe, ViChn, &tmpFlip, &tmpMirror);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_GetChnFlipMirror fail, ViChn[%d]\n", ViChn);

        if ((pstMirrorFlip->bFlip == tmpFlip) && (pstMirrorFlip->bMirror == tmpMirror)) {
            return MAPI_SUCCESS;
        }

        s32Ret = CVI_VI_SetChnFlipMirror(ViPipe, ViChn, pstMirrorFlip->bFlip, pstMirrorFlip->bMirror);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_SetChnFlipMirror fail, ViChn[%d]\n", ViChn);
        break;
    }
    default:
        break;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_CMD_E enCMD,
                            void *pAttr, uint32_t u32Len)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    CHECK_MAPI_VCAP_NULL_PTR_RET(pAttr);
    CHECK_MAPI_VCAP_MAX_VAL_RET("enCMD", enCMD, (MAPI_VCAP_CMD_BUTT - 1));
    CHECK_MAPI_VCAP_ZERO_VAL_RET("u32Len", u32Len);

    CVI_S32 s32Ret = MAPI_SUCCESS;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    VI_PIPE_ATTR_S stViPipeAttr;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    s32Ret = CVI_VI_GetPipeAttr(ViPipe, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_VI_GetPipeAttr(%d) failed!\n", ViPipe);
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr(%d) failed!\n", ViPipe);
    #endif

    switch (enCMD) {
    case MAPI_VCAP_CMD_Fps: {
        ISP_PUB_ATTR_S pubAttr;
        if (stViPipeAttr.bYuvBypassPath == CVI_TRUE) {
            CVI_LOGW("yuv sensor skip isp ops\n");
            return CVI_SUCCESS;
        }
        s32Ret = CVI_ISP_GetPubAttr(ViPipe, &pubAttr);
        CHECK_MAPI_VCAP_RET(s32Ret, "call ISP_GetPubAttr fail, ViPipe[%d]\n", ViPipe);

        *((CVI_FLOAT *)pAttr) = pubAttr.f32FrameRate;
        break;
    }
    case MAPI_VCAP_CMD_Rotate: {
        s32Ret = CVI_VI_GetChnRotation(ViPipe, ViChn, (ROTATION_E *)pAttr);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_GetChnRotation fail, ViChn[%d]\n", ViChn);
        break;
    }
    case MAPI_VCAP_CMD_MirrorFlip: {
        CVI_BOOL tmpFlip, tmpMirror;
        s32Ret = CVI_VI_GetChnFlipMirror(ViPipe, ViChn, &tmpFlip, &tmpMirror);
        CHECK_MAPI_VCAP_RET(s32Ret, "call VI_GetChnFlipMirror fail, ViChn[%d]\n", ViChn);

        ((MAPI_VCAP_MIRRORFLIP_ATTR_S *)pAttr)->bMirror = tmpMirror;
        ((MAPI_VCAP_MIRRORFLIP_ATTR_S *)pAttr)->bFlip = tmpFlip;
        break;
    }
    default:
        break;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetChnCropAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_CROP_INFO_S *pstCropInfo)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstCropInfo);
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;
    #endif

    if (CVI_VI_SetChnCrop(ViPipe, ViChn, pstCropInfo) != CVI_SUCCESS) {
        CVI_LOGE("CVI_VI_SetChnCrop failed\n");
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetChnCropAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_CROP_INFO_S *pstCropInfo)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstCropInfo);
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    VI_CHN         ViChn;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    #ifdef CHIP_184X
    ViChn  = 0;
    #else
    ViChn  = pstViInfo->stChnInfo.s32ChnId;
    #endif

    if (CVI_VI_GetChnCrop(ViPipe, ViChn, pstCropInfo) != CVI_SUCCESS) {
        CVI_LOGE("CVI_VI_GetChnCrop failed\n");
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetDumpRawAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_DUMP_ATTR_S *pstDumpAttr)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstDumpAttr);
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    if (CVI_VI_SetPipeDumpAttr(ViPipe, pstDumpAttr) != CVI_SUCCESS) {
        CVI_LOGE("VI_SetPipeDumpAttr failed\n");
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetDumpRawAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_DUMP_ATTR_S *pstDumpAttr)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstDumpAttr);
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    if (CVI_VI_GetPipeDumpAttr(ViPipe, pstDumpAttr) != CVI_SUCCESS) {
        CVI_LOGE("VI_GetPipeDumpAttr failed\n");
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

void *VcapDumpRAWthread(void *pArg)
{
    CVI_S32 s32Ret;
    uint32_t i;
    VIDEO_FRAME_INFO_S stVideoFrame[2];
    VCAP_DUMP_RAW_CTX_T *pstArg = NULL;
    MAPI_VCAP_RAW_DATA_T *pstCallbackFun = NULL;

    memset(stVideoFrame, 0, sizeof(stVideoFrame));

    stVideoFrame[0].stVFrame.enPixelFormat = PIXEL_FORMAT_RGB_BAYER_12BPP;
	stVideoFrame[1].stVFrame.enPixelFormat = PIXEL_FORMAT_RGB_BAYER_12BPP;

    pstArg = (VCAP_DUMP_RAW_CTX_T *)pArg;
    pstCallbackFun = (MAPI_VCAP_RAW_DATA_T *)&pstArg->stCallbackFun;

    for (i = 0; i < pstArg->u32Count; i++) {
        s32Ret = CVI_VI_GetPipeFrame(pstArg->ViPipe, stVideoFrame, 1000);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("Get %dth vcap frame timeout.\n", i);
            continue;
        }

        pstCallbackFun->pfn_VCAP_RawDataProc(pstArg->ViPipe, stVideoFrame, i,
                                             pstCallbackFun->pPrivateData);

        s32Ret = CVI_VI_ReleasePipeFrame(pstArg->ViPipe, stVideoFrame);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("Release %dth vcap frame error.\n", i);
        }
    }
    return NULL;
}

int MAPI_VCAP_StartDumpRaw(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, uint32_t u32Count,
                               MAPI_VCAP_RAW_DATA_T *pstVCapRawData)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstVCapRawData);
    CHECK_MAPI_VCAP_NULL_PTR_RET(pstVCapRawData->pfn_VCAP_RawDataProc);

    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    VI_PIPE        ViPipe;
    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    VI_DUMP_ATTR_S stDumpAttr;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    ViPipe = pstViInfo->stChnInfo.s32ChnId;

    if (CVI_VI_GetPipeDumpAttr(ViPipe, &stDumpAttr) != CVI_SUCCESS) {
        CVI_LOGE("VI_GetPipeDumpAttr failed\n");
        return MAPI_ERR_FAILURE;
    }

    if (stDumpAttr.bEnable != CVI_TRUE) {
        CVI_LOGE("Vcap dump raw is not been enabled.\n");
        return MAPI_ERR_FAILURE;
    }

    if (stDumpAttr.enDumpType != VI_DUMP_TYPE_RAW) {
        CVI_LOGE("Vcap dump type is not raw.\n");
        return MAPI_ERR_FAILURE;
    }

    if (u32Count == 0) {
        CVI_LOGE("Vcap dump raw number is zero.\n");
        return MAPI_ERR_FAILURE;
    }

    st->stVcapDumpRawCtx.bStart = CVI_TRUE;
    st->stVcapDumpRawCtx.ViPipe = ViPipe;
    st->stVcapDumpRawCtx.u32Count = u32Count;
    st->stVcapDumpRawCtx.stCallbackFun = *pstVCapRawData;

    s32Ret = pthread_create(&st->stVcapDumpRawCtx.pthreadDumpRaw, CVI_NULL,
                            VcapDumpRAWthread, (void *)&st->stVcapDumpRawCtx);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("Create vcap dump raw thread error.\n");
        st->stVcapDumpRawCtx.pthreadDumpRaw = 0;
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_StopDumpRaw(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);

    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;

    if ((long)st->stVcapDumpRawCtx.pthreadDumpRaw != -1) {
        s32Ret = pthread_join(st->stVcapDumpRawCtx.pthreadDumpRaw, CVI_NULL);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("pthread_join failed.\n");
        }
    }
    st->stVcapDumpRawCtx.bStart = CVI_FALSE;
    st->stVcapDumpRawCtx.pthreadDumpRaw = 0;
    st->stVcapDumpRawCtx.stCallbackFun.pfn_VCAP_RawDataProc = CVI_NULL;
    st->stVcapDumpRawCtx.stCallbackFun.pPrivateData = CVI_NULL;
    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetSensorPipeAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, int *status)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);

    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    // CVI_LOGD("%s(%d)\n", __FUNCTION__, sns_id);

    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;
    pstViInfo = &vt->ViConfig.astViInfo[sns_id];

    #ifdef CHIP_184X
    VI_PIPE        ViPipe;
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_FALSE) {
         *status = 1;
    }
    #else
    VI_PIPE_ATTR_S stViPipeAttr;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr failed!\n");

    if (stViPipeAttr.bYuvBypassPath == CVI_FALSE) {
        *status = 1;
    }
    #endif

    return s32Ret;
}

int MAPI_VCAP_GetSensorPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    //
    // TODO: FIXME
    //
    // When initializing VI, in astViInfo[id], astViInfo[i].stPipeInfo.aPipe[0]
    // is 0 and 1 for sensor 0 and sensor 1, and astViInfo[i].stChnInfo.s32ChnId is
    // always 0. However, when bind, or calling VI_GetChnFrame(), we always
    // pass pipeId as 0, and pass chnId as 0 or 1 respectively.
    // This is very confusing becasue it mixed up the definication or pipe and
    // chn.
    //
    // WR: always return 0 for GetSensorPipe
    //
#if 0
    return (int)vt->ViConfig.astViInfo[sns_id].stPipeInfo.ViPipe;
#else
    CVI_LOG_ASSERT(sns_id == (int)vt->ViConfig.astViInfo[sns_id].stChnInfo.s32ChnId,
            "sns_id %d not match with pipe Id %d\n",
            sns_id,
            (int)vt->ViConfig.astViInfo[sns_id].stChnInfo.s32ChnId);
    return 0;
#endif
}

int MAPI_VCAP_GetSensorChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;

    //
    // TODO: FIXME
    //
    // When initializing VI, in astViInfo[id], astViInfo[i].stPipeInfo.aPipe[0]
    // is 0 and 1 for sensor 0 and sensor 1, and astViInfo[i].stChnInfo.s32ChnId is
    // always 0. However, when bind, or calling CVI_VI_GetChnFrame(), we always
    // pass pipeId as 0, and pass chnId as 0 or 1 respectively.
    // This is very confusing becasue it mixed up the definication or pipe and
    // chn.
    //
    // WR: return sns_id for GetSensorChn
    //
#if 1
    return (int)vt->ViConfig.astViInfo[sns_id].stChnInfo.s32ChnId;
#else
    CVI_LOG_ASSERT(0 == (int)vt->ViConfig.astViInfo[sns_id].stChnInfo.s32ChnId,
            "sensor %d, chn_id %d is not zero\n",
            sns_id,
            (int)vt->ViConfig.astViInfo[sns_id].stChnInfo.s32ChnId);
    return sns_id;
#endif
}

int MAPI_VCAP_AutoFocusOnce(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe = 0;
    ISP_FOCUS_ATTR_S stFocusAttr = { 0 };
    ISP_FOCUS_Q_INFO_S stFocusInfo = { 0 };

    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);

    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    s32Ret = CVI_ISP_GetAFAttr(ViPipe, &stFocusAttr);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_ISP_GetAFAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
        return MAPI_ERR_FAILURE;
    }

    if (stFocusAttr.enOpType == OP_TYPE_AUTO) {
        /* 自动对焦模式下，只有空闲/完成状态才触发，处理中不重复触发 */
        s32Ret = CVI_ISP_AFQueryFocusInfo(ViPipe, &stFocusInfo);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("CVI_ISP_AFQueryFocusInfo failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
            return MAPI_ERR_FAILURE;
        }
        if (stFocusInfo.eStatus != AF_INIT) {
            CVI_LOGI("AF is busy, skip trigger, ViPipe=%d status=%d focus_pos=%u\n",
                ViPipe, stFocusInfo.eStatus, stFocusInfo.u16FocusPos);
            return MAPI_SUCCESS;
        }

        /* 开启了自动对焦，在空闲状态调用即可触发 */
        s32Ret = CVI_ISP_AFAutoFocus(ViPipe);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("CVI_ISP_AFAutoFocus failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
            return MAPI_ERR_FAILURE;
        }
    } else if (stFocusAttr.enOpType == OP_TYPE_MANUAL) {
        /* 手动对焦的情况下，也可以通过这种方式触发一次对焦 */
        stFocusAttr.stManual.enManualOpType = OP_TYPE_AUTO_FOCUS;
        s32Ret = CVI_ISP_SetAFAttr(ViPipe, &stFocusAttr);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("CVI_ISP_SetAFAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
            return MAPI_ERR_FAILURE;
        }
    } else {
        CVI_LOGE("unsupported AF op type, ViPipe=%d enOpType=%d\n", ViPipe, stFocusAttr.enOpType);
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetAutoFocusEnable(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, bool enable, uint32_t poll_times)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe = 0;
    ISP_FOCUS_ATTR_S stFocusAttr = { 0 };
    ISP_FOCUS_Q_INFO_S stFocusInfo = { 0 };
    uint32_t i = 0;

    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);

    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    s32Ret = CVI_ISP_GetAFAttr(ViPipe, &stFocusAttr);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_ISP_GetAFAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
        return MAPI_ERR_FAILURE;
    }

    if (enable) {
        stFocusAttr.enOpType = OP_TYPE_AUTO;
        stFocusAttr.stManual.enManualOpType = OP_TYPE_AUTO_FOCUS;
    } else {
        /* 关闭AF时，需要先判断自动对焦是否完成，以便锁定的位置是一个清晰的位置。*/
        s32Ret = CVI_ISP_AFQueryFocusInfo(ViPipe, &stFocusInfo);
        if (s32Ret != CVI_SUCCESS) {
            CVI_LOGE("CVI_ISP_AFQueryFocusInfo failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
            return MAPI_ERR_FAILURE;
        }
        CVI_LOGD("focus status=%d focus_pos=%u\n", stFocusInfo.eStatus, stFocusInfo.u16FocusPos);

        if (poll_times > 0 && stFocusInfo.eStatus != AF_INIT) {
            CVI_LOGI("wait focus done before disable, poll_times=%u\n", poll_times);
            for (i = 0; i < poll_times; ++i) {
                usleep(100 * 1000);
                s32Ret = CVI_ISP_AFQueryFocusInfo(ViPipe, &stFocusInfo);
                if (s32Ret != CVI_SUCCESS) {
                    CVI_LOGE("CVI_ISP_AFQueryFocusInfo poll failed, ViPipe=%d idx=%u ret=0x%x\n",
                        ViPipe, i, s32Ret);
                    return MAPI_ERR_FAILURE;
                }
                if (stFocusInfo.eStatus == AF_INIT) {
                    CVI_LOGI("focus ready, idx=%u focus_pos=%u\n", i, stFocusInfo.u16FocusPos);
                    break;
                }
            }

            if (stFocusInfo.eStatus != AF_INIT) {
                CVI_LOGW("focus not ready after polling, status=%d\n", stFocusInfo.eStatus);
            }
        }

        stFocusAttr.enOpType = OP_TYPE_MANUAL;
        stFocusAttr.stManual.enManualOpType = OP_TYPE_FOCUS_POS;
        stFocusAttr.stManual.u16ManualPos = stFocusInfo.u16FocusPos;
    }

    s32Ret = CVI_ISP_SetAFAttr(ViPipe, &stFocusAttr);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_ISP_SetAFAttr failed, ViPipe=%d enable=%d ret=0x%x\n",
            ViPipe, enable, s32Ret);
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetExposureAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_EXPOSURE_ATTR_S *pstExpAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstExpAttr);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_GetExposureAttr(ViPipe, pstExpAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetExposureAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetExposureAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_EXPOSURE_ATTR_S *pstExpAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstExpAttr);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_SetExposureAttr(ViPipe, pstExpAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetExposureAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetAntiFlicker(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, CVI_U8 *pu8Enable, CVI_U8 *pu8Frequency)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pu8Enable);
    CHECK_MAPI_VCAP_NULL_PTR_RET(pu8Frequency);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_GetAntiFlicker(ViPipe, pu8Enable, pu8Frequency);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetAntiFlicker failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetAntiFlicker(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, CVI_U8 u8Enable, CVI_U8 u8Frequency)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    s32Ret = CVI_ISP_SetAntiFlicker(ViPipe, u8Enable, u8Frequency);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetAntiFlicker failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetAWBAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_AWB_ATTR_EX_S *pstAWBAttrEx)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstAWBAttrEx);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_GetAWBAttrEx(ViPipe, pstAWBAttrEx);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetAWBAttrEx failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetAWBAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_AWB_ATTR_EX_S *pstAWBAttrEx)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstAWBAttrEx);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_SetAWBAttrEx(ViPipe, pstAWBAttrEx);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetAWBAttrEx failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetCAAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_CA_ATTR_S *pstCAAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstCAAttr);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_GetCAAttr(ViPipe, pstCAAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetCAAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetCAAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_CA_ATTR_S *pstCAAttr)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe;

    CHECK_MAPI_VCAP_NULL_PTR_RET(pstCAAttr);
    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);

    s32Ret = CVI_ISP_SetCAAttr(ViPipe, pstCAAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetCAAttr failed, ViPipe=%d ret=0x%x\n", ViPipe, s32Ret);
    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetFrame(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl,
        VIDEO_FRAME_INFO_S *frame)
{
    VI_PIPE ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    VI_CHN ViChn = MAPI_VCAP_GetSensorChn(sns_hdl);

    #ifdef CHIP_184X
    ViChn = 0;
    #endif
    if (CVI_VI_GetChnFrame(ViPipe, ViChn, frame, 1000) != CVI_SUCCESS) {
        CVI_LOGE("VI_GetChnFrame ViPipe(%d) ViChn(%d) failed\n",
                    ViPipe, ViChn);
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_ReleaseFrame(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl,
        VIDEO_FRAME_INFO_S *frame)
{
    VI_PIPE ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    VI_CHN ViChn = MAPI_VCAP_GetSensorChn(sns_hdl);

    #ifdef CHIP_184X
    ViChn = 0;
    #endif

    if (CVI_VI_ReleaseChnFrame(ViPipe, ViChn, frame) != CVI_SUCCESS) {
        CVI_LOGE("VI_ReleaseChnFrame ViPipe(%d) ViChn(%d) failed\n",
                    ViPipe, ViChn);
        return MAPI_ERR_FAILURE;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetGeneralVcapAttr(MAPI_VCAP_ATTR_T *vcap_attr, uint8_t u8DevNum)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(vcap_attr);

    getVcapAttr(vcap_attr);
    vcap_attr->u8DevNum = u8DevNum;

    if ((vcap_attr->u8DevNum == 0) || (vcap_attr->u8DevNum > VI_MAX_DEV_NUM)) {
        CVI_LOGE("sensor dev number (%d) error\n", vcap_attr->u8DevNum);
        return MAPI_ERR_INVALID;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetPqBinPath(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl)
{
    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);

    CVI_S32 s32Ret;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    MAPI_VCAP_CTX_T *vt = (MAPI_VCAP_CTX_T *)st->vcap_hdl;
    int sns_id = st->sns_id;
    uint8_t u8WdrMode = 0;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    MAPI_VI_INFO_S *pstViInfo = CVI_NULL;

    pstViInfo = &vt->ViConfig.astViInfo[sns_id];
    u8WdrMode = st->attr.attr_sns[sns_id].u8WdrMode;

    #ifdef CHIP_184X
    VI_PIPE        ViPipe;
    ViPipe = pstViInfo->stChnInfo.s32ChnId;
    SENSOR_CFG_S *pstSnsCfg = CVI_NULL;
    pstSnsCfg = &vt->stSnsCfg;

    if (pstSnsCfg->sns_cfg.bBypassIsp[ViPipe] == CVI_FALSE) {
        s32Ret = CVI_BIN_SetBinName(u8WdrMode, PqBinName[u8WdrMode]);
    }
    #else
    VI_PIPE_ATTR_S stViPipeAttr;
    s32Ret = MAPI_VCAP_GetPipeAttr(pstViInfo->stSnsInfo.enSnsType, &stViPipeAttr);
    CHECK_MAPI_VCAP_RET(s32Ret, "MAPI_VCAP_GetPipeAttr failed!\n");

    if (stViPipeAttr.bYuvBypassPath == CVI_FALSE) {
        s32Ret = CVI_BIN_SetBinName(u8WdrMode, PqBinName[u8WdrMode]);
    }
    #endif

    return s32Ret;
}

int MAPI_VCAP_SetEffect(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, char* effect_bin_path)
{
    CVI_S32 s32Ret = MAPI_SUCCESS;
    MAPI_VCAP_SENSOR_T *st = (MAPI_VCAP_SENSOR_T *)sns_hdl;
    CVI_S32 sns_id = st->sns_id;
    CVI_U8 wdr_mode = st->attr.attr_sns[sns_id].u8WdrMode;
    CVI_LOGI("%s(%d)\n", __FUNCTION__, sns_id);

    if (!effect_bin_path) {
        CVI_LOGE("effect is invaild\n");
        return MAPI_ERR_INVALID;
    } else {
        CVI_LOGI("effect pqbin name: %s\n", effect_bin_path);
    }

    s32Ret = CVI_BIN_SetBinName(wdr_mode, effect_bin_path);
    if (s32Ret != CVI_SUCCESS) {
        CVI_LOGE("CVI_BIN_SetBinName failed\n");
        return MAPI_ERR_INVALID;
    }

    s32Ret = MAPI_VCAP_BIN_ReadParaFrombin(s_BinId[sns_id]);
    if (s32Ret != MAPI_SUCCESS) {
        CVI_LOGE("MAPI_VCAP_BIN_ReadParaFrombin failed\n");
        return MAPI_ERR_INVALID;
    }

    return MAPI_SUCCESS;
}

int MAPI_VCAP_GetLscData(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_LSC_DATA_T* lsc_data)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe = 0;

    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    CHECK_MAPI_VCAP_NULL_PTR_RET(lsc_data);

    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    if (ViPipe < 0) {
        CVI_LOGE("invalid ViPipe\n");
        return MAPI_ERR_INVALID;
    }

    memset(lsc_data, 0, sizeof(*lsc_data));
    lsc_data->lsc_valid = 1;

    s32Ret = CVI_ISP_GetMeshShadingAttr(ViPipe, &lsc_data->mlsc_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetMeshShadingAttr failed\n");

    s32Ret = CVI_ISP_GetMeshShadingGainLutAttr(ViPipe, &lsc_data->mlsc_gain_lut_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetMeshShadingGainLutAttr failed\n");

    s32Ret = CVI_ISP_GetRadialShadingAttr(ViPipe, &lsc_data->rlsc_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetRadialShadingAttr failed\n");

    s32Ret = CVI_ISP_GetRadialShadingGainLutAttr(ViPipe, &lsc_data->rlsc_gain_lut_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_GetRadialShadingGainLutAttr failed\n");

    return MAPI_SUCCESS;
}

int MAPI_VCAP_SetLscData(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, const MAPI_VCAP_LSC_DATA_T* lsc_data)
{
    CVI_S32 s32Ret = CVI_SUCCESS;
    VI_PIPE ViPipe = 0;

    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    CHECK_MAPI_VCAP_NULL_PTR_RET(lsc_data);

    if (lsc_data->lsc_valid == 0) {
        CVI_LOGW("skip lsc apply due to invalid flag\n");
        return MAPI_ERR_INVALID;
    }

    ViPipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    if (ViPipe < 0) {
        CVI_LOGE("invalid ViPipe\n");
        return MAPI_ERR_INVALID;
    }

    s32Ret = CVI_ISP_SetRadialShadingAttr(ViPipe, (ISP_RADIAL_SHADING_ATTR_S*)&lsc_data->rlsc_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetRadialShadingAttr failed\n");

    s32Ret = CVI_ISP_SetRadialShadingGainLutAttr(
        ViPipe, (ISP_RADIAL_SHADING_GAIN_LUT_ATTR_S*)&lsc_data->rlsc_gain_lut_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetRadialShadingGainLutAttr failed\n");

    s32Ret = CVI_ISP_SetMeshShadingAttr(ViPipe, (ISP_MESH_SHADING_ATTR_S*)&lsc_data->mlsc_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetMeshShadingAttr failed\n");

    s32Ret = CVI_ISP_SetMeshShadingGainLutAttr(
        ViPipe, (ISP_MESH_SHADING_GAIN_LUT_ATTR_S*)&lsc_data->mlsc_gain_lut_attr);
    CHECK_MAPI_VCAP_RET(s32Ret, "CVI_ISP_SetMeshShadingGainLutAttr failed\n");

    return MAPI_SUCCESS;
}

typedef enum {
    MAPI_RAW_UNCOMPRESS_UNPACK,
    MAPI_RAW_UNCOMPRESS_PACK,
    MAPI_RAW_COMPRESS,
} MAPI_RAW_PACK_MODE_E;

#define MAPI_ARRAY_CPY(dst, src, size)      \
    do {                                    \
        for (int _k = 0; _k < (size); ++_k) \
            (dst)[_k] = (src)[_k];          \
    } while (0)

#define MAPI_FREE_PTR(ptr)   \
    do {                     \
        if ((ptr) != NULL) { \
            free((ptr));     \
            (ptr) = NULL;    \
        }                    \
    } while (0)

/*
 * These symbols are provided by CVI's isp_algo library (libisp_algo).
 * The matching header lives under cvi_mpi's internal isp_algo tree instead of
 * the public SDK export path, so keep explicit extern declarations here.
 */
extern int isp_algo_lsc_calibration(uint16_t* raw_image, int width, int height, int bayer_id,
    int num_knot_y, int num_knot_x, int calib_flag, int fisheye_flag,
    int ob_rr, int ob_gr, int ob_gb, int ob_bb,
    int* center_x, int* center_y, int* radius, int* norm,
    int* lsc_r_gain,
    int* lsc_g_gain,
    int* lsc_b_gain,
    int* lsc_radius_gain);

#define MAPI_DPCM_COMPRESSMODE (7)
#define MAPI_DPCM_PREDICTOR_BYTES (8)
#define MAPI_DPCM_TILE_MODE_WIDTH (2304)
#define MAPI_DPCM_TITLE_START_IDX (1536)
#define MAPI_LSC_CALIB_DOWNSAMPLE (2)

static CVI_U16 _MAPI_DecoDpcm(CVI_U16 xp, bool s, CVI_U16 v)
{
    return s ? ((xp > v) ? (xp - v) : 0) : (((xp + v) < 4095) ? (xp + v) : 4095);
}

static CVI_U16 _MAPI_DecpPcm(CVI_U16 xp, CVI_U16 ad, CVI_U16 v)
{
    return (v > xp) ? (v + ad) : (v + ad + 1);
}

static CVI_U16 _MAPI_Decode12_6_12(bool nop, CVI_U16 xenco, CVI_U16 xpred)
{
    if (nop) {
        return (xenco << 6) + 32;
    } else if ((xenco & 0x3c) == 0x00) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x2, xenco & 0x1);
    } else if ((xenco & 0x3c) == 0x04) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x2, ((xenco & 0x1) << 2) + 3);
    } else if ((xenco & 0x38) == 0x10) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x4, ((xenco & 0x3) << 3) + 13);
    } else if ((xenco & 0x3c) == 0x08) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x2, ((xenco & 0x1) << 4) + 49);
    } else if ((xenco & 0x38) == 0x18) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x4, ((xenco & 0x3) << 5) + 89);
    } else if ((xenco & 0x3c) == 0x0c) {
        return _MAPI_DecoDpcm(xpred, xenco & 0x2, ((xenco & 0x1) << 6) + 233);
    }
    return _MAPI_DecpPcm(xpred, 63, (xenco & 0x1f) << 7);
}

static CVI_U16 _MAPI_Predictor2(CVI_U16 x, CVI_U16* pb, bool* nop)
{
    CVI_U16 xpred = 0;

    *nop = (x == 0);
    if (x == 1) {
        xpred = pb[0];
    } else if (x == 2) {
        xpred = pb[1];
    } else if (x == 3) {
        xpred = ((pb[0] <= pb[1] && pb[1] <= pb[2]) || (pb[0] >= pb[1] && pb[1] >= pb[2])) ? pb[0] : pb[1];
    } else if (x >= 4) {
        if ((pb[0] <= pb[1] && pb[1] <= pb[2]) || (pb[0] >= pb[1] && pb[1] >= pb[2])) {
            xpred = pb[0];
        } else if ((pb[0] <= pb[2] && pb[1] <= pb[3]) || (pb[0] >= pb[2] && pb[1] >= pb[3])) {
            xpred = pb[1];
        } else {
            xpred = (pb[1] + pb[3] + 1) >> 1;
        }
    }
    return (*nop) ? 0 : xpred;
}

static CVI_U16 _MAPI_GetPacked6bSymbol(const CVI_U8* line, CVI_U32 symbol_idx)
{
    CVI_U32 chunk = symbol_idx / 4;
    CVI_U32 rem = symbol_idx % 4;
    const CVI_U8* p = line + chunk * 3;
    CVI_U8 b0 = p[0];
    CVI_U8 b1 = p[1];
    CVI_U8 b2 = p[2];

    if (rem == 0) {
        return (CVI_U16)(((b0 & 0x03) << 4) | (b2 & 0x0F));
    } else if (rem == 1) {
        return (CVI_U16)((b0 & 0xFC) >> 2);
    } else if (rem == 2) {
        return (CVI_U16)(((b2 & 0xF0) >> 4) | ((b1 & 0x03) << 4));
    }
    return (CVI_U16)((b1 & 0xFC) >> 2);
}

static bool _MAPI_VCAP_GetHalfSampleCoord(CVI_U32 src_coord, CVI_U32 dst_limit, CVI_U32* dst_coord)
{
    CVI_U32 mod = src_coord & 0x3;

    if (mod != 0 && mod != 3) {
        return false;
    }

    *dst_coord = src_coord >> 1;
    return *dst_coord < dst_limit;
}

static int _MAPI_VCAP_DecodeCompressedRawToU16(const CVI_U8* raw_pack, uint16_t* raw_unpack,
    CVI_U32 width, CVI_U32 height, CVI_U32 dst_width, CVI_U32 dst_height, CVI_U32 stride)
{
    CVI_U32 line_bytes = ((width + 3) / 4) * 3;
    CVI_U32 tile_line_bytes = ((width + MAPI_DPCM_PREDICTOR_BYTES + 3) / 4) * 3;
    bool tile_mode = (width > MAPI_DPCM_TILE_MODE_WIDTH && stride >= tile_line_bytes);

    if (raw_pack == NULL || raw_unpack == NULL || width == 0 || height == 0 || dst_width == 0 || dst_height == 0 || stride < line_bytes) {
        CVI_LOGE("LSC decode compressed raw invalid input: raw_pack=%p raw_unpack=%p w=%u h=%u dst_w=%u dst_h=%u stride=%u min_line=%u\n",
            raw_pack, raw_unpack, width, height, dst_width, dst_height, stride, line_bytes);
        return MAPI_ERR_INVALID;
    }

    CVI_LOGI("LSC decode compressed raw half-size direct-to-u16: src=%ux%u dst=%ux%u stride=%u line=%u tile_line=%u tile=%d mode=%d\n",
        width, height, dst_width, dst_height, stride, line_bytes, tile_line_bytes, tile_mode, MAPI_DPCM_COMPRESSMODE);
    for (CVI_U32 y = 0; y < height; ++y) {
        const CVI_U8* line = raw_pack + y * stride;
        CVI_U16 pix_buf[4] = { 0 };
        CVI_U32 dst_y = 0;
        bool sample_y = _MAPI_VCAP_GetHalfSampleCoord(y, dst_height, &dst_y);

        for (CVI_U32 x = 0; x < width; ++x) {
            CVI_U32 packed_idx = x;
            CVI_U32 dst_x = 0;
            bool nop = false;
            CVI_U16 xpred = 0;
            CVI_U16 xenco = 0;
            CVI_U16 xdeco = 0;

            if (tile_mode && x >= MAPI_DPCM_TITLE_START_IDX) {
                packed_idx += MAPI_DPCM_PREDICTOR_BYTES;
            }

            xenco = _MAPI_GetPacked6bSymbol(line, packed_idx);
            xpred = _MAPI_Predictor2((CVI_U16)x, pix_buf, &nop);
            xdeco = _MAPI_Decode12_6_12(nop, xenco, xpred);
            if (sample_y && _MAPI_VCAP_GetHalfSampleCoord(x, dst_width, &dst_x)) {
                raw_unpack[dst_y * dst_width + dst_x] = (uint16_t)xdeco;
            }

            pix_buf[3] = pix_buf[2];
            pix_buf[2] = pix_buf[1];
            pix_buf[1] = pix_buf[0];
            pix_buf[0] = xdeco;
        }
    }

    return MAPI_SUCCESS;
}

static int _MAPI_VCAP_GetRawInfo(VI_PIPE vi_pipe, VI_CHN vi_chn,
    MAPI_RAW_PACK_MODE_E* raw_pack_mode, BAYER_FORMAT_E* bayer_format,
    CVI_U32* raw_size, CVI_U32* width, CVI_U32* height)
{
    VI_CHN_ATTR_S stChnAttr;
    VI_DEV_ATTR_S stDevAttr;
    COMPRESS_MODE_E compress_mode;

    if (CVI_VI_GetChnAttr(vi_pipe, vi_chn, &stChnAttr) != CVI_SUCCESS) {
        return MAPI_ERR_FAILURE;
    }
    if (CVI_VI_GetDevAttr(vi_pipe, &stDevAttr) != CVI_SUCCESS) {
        return MAPI_ERR_FAILURE;
    }

    compress_mode = stChnAttr.enCompressMode;
    *width = stChnAttr.stSize.u32Width;
    *height = stChnAttr.stSize.u32Height;
    *bayer_format = stDevAttr.enBayerFormat;
    *raw_size = VI_GetRawBufferSize(*width, *height, PIXEL_FORMAT_RGB_BAYER_12BPP,
        compress_mode, DEFAULT_ALIGN, CVI_FALSE);
    *raw_pack_mode = (compress_mode == COMPRESS_MODE_NONE) ? MAPI_RAW_UNCOMPRESS_PACK : MAPI_RAW_COMPRESS;

    return MAPI_SUCCESS;
}

static int _MAPI_VCAP_GetRawData(VI_PIPE vi_pipe, CVI_U8* raw_data, CVI_U32 raw_size, CVI_U32 height, CVI_U32* stride)
{
    VI_DUMP_ATTR_S dump_attr;
    VIDEO_FRAME_INFO_S stFrame[4];
    CVI_S32 ret = CVI_SUCCESS;

    memset(&dump_attr, 0, sizeof(dump_attr));
    dump_attr.bEnable = CVI_TRUE;
    dump_attr.u32Depth = 0;
    dump_attr.enDumpType = VI_DUMP_TYPE_RAW;
    ret = CVI_VI_SetPipeDumpAttr(vi_pipe, &dump_attr);
    if (ret != CVI_SUCCESS) {
        return MAPI_ERR_FAILURE;
    }

    memset(stFrame, 0, sizeof(stFrame));
    ret = CVI_VI_GetPipeFrame(vi_pipe, stFrame, 1000);
    if (ret != CVI_SUCCESS) {
        return MAPI_ERR_FAILURE;
    }

    usleep(50 * 1000);
    CVI_LOGI("LSC raw frame: phy=0x%llx len=%u expect=%u stride=%u w=%u h=%u compress=%d pixel=%d\n",
        stFrame[0].stVFrame.u64PhyAddr[0], stFrame[0].stVFrame.u32Length[0], raw_size,
        stFrame[0].stVFrame.u32Stride[0], stFrame[0].stVFrame.u32Width,
        stFrame[0].stVFrame.u32Height, stFrame[0].stVFrame.enCompressMode,
        stFrame[0].stVFrame.enPixelFormat);

    if (stFrame[0].stVFrame.u64PhyAddr[0] == 0 || stFrame[0].stVFrame.u32Length[0] != raw_size) {
        CVI_VI_ReleasePipeFrame(vi_pipe, stFrame);
        return MAPI_ERR_FAILURE;
    }

    CVI_LOGI("LSC mmap raw frame before: phy=0x%llx size=%u\n",
        stFrame[0].stVFrame.u64PhyAddr[0], raw_size);
    stFrame[0].stVFrame.pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(stFrame[0].stVFrame.u64PhyAddr[0], raw_size);
    CVI_LOGI("LSC mmap raw frame after: va=%p size=%u\n",
        stFrame[0].stVFrame.pu8VirAddr[0], raw_size);
    if (stFrame[0].stVFrame.pu8VirAddr[0] == NULL) {
        CVI_VI_ReleasePipeFrame(vi_pipe, stFrame);
        return MAPI_ERR_NOMEM;
    }
    *stride = stFrame[0].stVFrame.u32Stride[0];
    if (*stride == 0 && height != 0 && raw_size % height == 0) {
        *stride = raw_size / height;
        CVI_LOGW("LSC raw frame stride is 0, fallback to raw_size/height=%u\n", *stride);
    }
    memcpy(raw_data, stFrame[0].stVFrame.pu8VirAddr[0], raw_size);
    CVI_SYS_Munmap((void*)stFrame[0].stVFrame.pu8VirAddr[0], raw_size);
    CVI_VI_ReleasePipeFrame(vi_pipe, stFrame);

    return MAPI_SUCCESS;
}

static int _MAPI_VCAP_UnpackRaw(CVI_U8* raw_pack, uint16_t* raw_unpack,
    CVI_U32 width, CVI_U32 height, CVI_U32 dst_width, CVI_U32 dst_height,
    CVI_U32 stride, MAPI_RAW_PACK_MODE_E raw_pack_mode)
{
    if (raw_pack == NULL || raw_unpack == NULL || dst_width == 0 || dst_height == 0) {
        return MAPI_ERR_INVALID;
    }

    CVI_LOGI("LSC unpack raw half-size: src=%ux%u dst=%ux%u stride=%u pack_mode=%d\n",
        width, height, dst_width, dst_height, stride, raw_pack_mode);

    if (raw_pack_mode == MAPI_RAW_UNCOMPRESS_UNPACK) {
        uint16_t* src = (uint16_t*)raw_pack;
        for (CVI_U32 h = 0; h < height; ++h) {
            CVI_U32 dst_h = 0;
            if (!_MAPI_VCAP_GetHalfSampleCoord(h, dst_height, &dst_h))
                continue;
            for (CVI_U32 w = 0; w < width; ++w) {
                CVI_U32 dst_w = 0;
                if (_MAPI_VCAP_GetHalfSampleCoord(w, dst_width, &dst_w))
                    raw_unpack[dst_h * dst_width + dst_w] = src[h * width + w];
            }
        }
        return MAPI_SUCCESS;
    }

    if (raw_pack_mode == MAPI_RAW_UNCOMPRESS_PACK) {
        CVI_U8* line_buffer;
        for (CVI_U32 h = 0; h < height; ++h) {
            CVI_U32 dst_h = 0;
            bool sample_h = _MAPI_VCAP_GetHalfSampleCoord(h, dst_height, &dst_h);
            line_buffer = raw_pack + h * stride;
            for (CVI_U32 w = 0; w < width / 2; ++w) {
                CVI_U32 src_w0 = w * 2;
                CVI_U32 src_w1 = src_w0 + 1;
                CVI_U32 dst_w = 0;
                CVI_U8 v0 = *line_buffer++;
                CVI_U8 v1 = *line_buffer++;
                CVI_U8 v2 = *line_buffer++;
                if (sample_h && _MAPI_VCAP_GetHalfSampleCoord(src_w0, dst_width, &dst_w))
                    raw_unpack[dst_h * dst_width + dst_w] = ((v0 << 4) | ((v2 >> 0) & 0x0f));
                if (sample_h && _MAPI_VCAP_GetHalfSampleCoord(src_w1, dst_width, &dst_w))
                    raw_unpack[dst_h * dst_width + dst_w] = ((v1 << 4) | ((v2 >> 4) & 0x0f));
            }
        }
        return MAPI_SUCCESS;
    }

    return _MAPI_VCAP_DecodeCompressedRawToU16(raw_pack, raw_unpack, width, height,
        dst_width, dst_height, stride);
}

static int _MAPI_VCAP_UpdateMlscGainLutAttr(ISP_MESH_SHADING_GAIN_LUT_ATTR_S* mlsc_gain_lut_attr, CVI_U16 color_temp,
    int* lsc_r_gain, int* lsc_g_gain, int* lsc_b_gain)
{
    int tbl_size = mlsc_gain_lut_attr->Size;
    int insert_idx = -1;
    int real_color_temp_num = 0;
    ISP_MESH_SHADING_GAIN_LUT_S* p_gain_lut = mlsc_gain_lut_attr->LscGainLut;

    for (int i = 0; i < tbl_size; ++i) {
        if (p_gain_lut[i].ColorTemperature != 0)
            real_color_temp_num++;
    }

    mlsc_gain_lut_attr->Size = real_color_temp_num;
    tbl_size = real_color_temp_num;
    if (tbl_size == 0) {
        p_gain_lut[0].ColorTemperature = color_temp;
        MAPI_ARRAY_CPY(p_gain_lut[0].RGain, lsc_r_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[0].GGain, lsc_g_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[0].BGain, lsc_b_gain, CVI_ISP_LSC_GRID_POINTS);
        mlsc_gain_lut_attr->Size = 1;
        return MAPI_SUCCESS;
    }

    for (int i = 0; i < tbl_size; ++i) {
        if (color_temp == p_gain_lut[i].ColorTemperature) {
            MAPI_ARRAY_CPY(p_gain_lut[i].RGain, lsc_r_gain, CVI_ISP_LSC_GRID_POINTS);
            MAPI_ARRAY_CPY(p_gain_lut[i].GGain, lsc_g_gain, CVI_ISP_LSC_GRID_POINTS);
            MAPI_ARRAY_CPY(p_gain_lut[i].BGain, lsc_b_gain, CVI_ISP_LSC_GRID_POINTS);
            return MAPI_SUCCESS;
        }
    }

    if (color_temp < p_gain_lut[0].ColorTemperature) {
        insert_idx = 0;
    } else if (color_temp > p_gain_lut[tbl_size - 1].ColorTemperature) {
        insert_idx = tbl_size;
    } else {
        for (int i = 0; i < tbl_size - 1; ++i) {
            if (color_temp > p_gain_lut[i].ColorTemperature && color_temp < p_gain_lut[i + 1].ColorTemperature) {
                insert_idx = i + 1;
                break;
            }
        }
    }

    if (tbl_size == ISP_MLSC_COLOR_TEMPERATURE_SIZE) {
        int diff_pre = color_temp - p_gain_lut[insert_idx - 1].ColorTemperature;
        int diff_post = p_gain_lut[insert_idx].ColorTemperature - color_temp;
        if (diff_pre < diff_post)
            insert_idx--;
        p_gain_lut[insert_idx].ColorTemperature = color_temp;
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].RGain, lsc_r_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].GGain, lsc_g_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].BGain, lsc_b_gain, CVI_ISP_LSC_GRID_POINTS);
        return MAPI_SUCCESS;
    }

    if (insert_idx == tbl_size) {
        p_gain_lut[insert_idx].ColorTemperature = color_temp;
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].RGain, lsc_r_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].GGain, lsc_g_gain, CVI_ISP_LSC_GRID_POINTS);
        MAPI_ARRAY_CPY(p_gain_lut[insert_idx].BGain, lsc_b_gain, CVI_ISP_LSC_GRID_POINTS);
    } else {
        for (int i = tbl_size - 1; i >= insert_idx; --i) {
            MAPI_ARRAY_CPY(p_gain_lut[i + 1].RGain, p_gain_lut[i].RGain, CVI_ISP_LSC_GRID_POINTS);
            MAPI_ARRAY_CPY(p_gain_lut[i + 1].GGain, p_gain_lut[i].GGain, CVI_ISP_LSC_GRID_POINTS);
            MAPI_ARRAY_CPY(p_gain_lut[i + 1].BGain, p_gain_lut[i].BGain, CVI_ISP_LSC_GRID_POINTS);
            p_gain_lut[i + 1].ColorTemperature = p_gain_lut[i].ColorTemperature;
            if (i == insert_idx) {
                p_gain_lut[insert_idx].ColorTemperature = color_temp;
                MAPI_ARRAY_CPY(p_gain_lut[insert_idx].RGain, lsc_r_gain, CVI_ISP_LSC_GRID_POINTS);
                MAPI_ARRAY_CPY(p_gain_lut[insert_idx].GGain, lsc_g_gain, CVI_ISP_LSC_GRID_POINTS);
                MAPI_ARRAY_CPY(p_gain_lut[insert_idx].BGain, lsc_b_gain, CVI_ISP_LSC_GRID_POINTS);
            }
        }
    }

    mlsc_gain_lut_attr->Size += 1;
    return MAPI_SUCCESS;
}

int MAPI_VCAP_CalibrateLsc(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, int color_temp)
{
    int ret = MAPI_SUCCESS;
    VI_PIPE vi_pipe = MAPI_VCAP_GetSensorPipe(sns_hdl);
    VI_CHN vi_chn = MAPI_VCAP_GetSensorChn(sns_hdl);
    CVI_U32 width = 0, height = 0, stride = 0, raw_size = 0;
    CVI_U32 calib_width = 0, calib_height = 0;
    CVI_U8* raw_data = NULL;
    uint16_t* raw_data_unpack = NULL;
    BAYER_FORMAT_E bayer_id = BAYER_FORMAT_BG;
    MAPI_RAW_PACK_MODE_E raw_pack_mode = MAPI_RAW_UNCOMPRESS_UNPACK;
    ISP_BLACK_LEVEL_ATTR_S blc_attr;
    ISP_MESH_SHADING_ATTR_S mlsc_attr;
    ISP_MESH_SHADING_GAIN_LUT_ATTR_S mlsc_gain_lut_attr;
    ISP_RADIAL_SHADING_ATTR_S rlsc_attr;
    ISP_RADIAL_SHADING_GAIN_LUT_ATTR_S rlsc_gain_lut_attr;
    int center_x = 0, center_y = 0, radius = 0, norm = 0;
    int *lsc_r_gain = NULL, *lsc_g_gain = NULL, *lsc_b_gain = NULL, *lsc_radius_gain = NULL;
    CVI_U32 unpack_size = 0;
    CVI_U32 gain_size = 0;

    CHECK_MAPI_VCAP_NULL_PTR_RET(sns_hdl);
    if (vi_pipe < 0 || vi_chn < 0) {
        return MAPI_ERR_INVALID;
    }

    ret = _MAPI_VCAP_GetRawInfo(vi_pipe, vi_chn, &raw_pack_mode, &bayer_id, &raw_size, &width, &height);
    if (ret != MAPI_SUCCESS)
        return ret;
    if ((width % MAPI_LSC_CALIB_DOWNSAMPLE) != 0 || (height % MAPI_LSC_CALIB_DOWNSAMPLE) != 0) {
        CVI_LOGE("LSC calibrate invalid downsample source size: w=%u h=%u factor=%d\n",
            width, height, MAPI_LSC_CALIB_DOWNSAMPLE);
        return MAPI_ERR_INVALID;
    }
    calib_width = width / MAPI_LSC_CALIB_DOWNSAMPLE;
    calib_height = height / MAPI_LSC_CALIB_DOWNSAMPLE;
    unpack_size = calib_width * calib_height * (CVI_U32)sizeof(uint16_t);
    CVI_LOGI("LSC calibrate start: pipe=%d chn=%d src=%ux%u calib=%ux%u raw_size=%u unpack_size=%u pack_mode=%d ct=%d\n",
        vi_pipe, vi_chn, width, height, calib_width, calib_height, raw_size,
        unpack_size, raw_pack_mode, color_temp);

    CVI_LOGI("LSC alloc raw_data before: size=%u bytes\n", raw_size);
    raw_data = (CVI_U8*)calloc(raw_size, 1);
    CVI_LOGI("LSC alloc raw_data after: ptr=%p size=%u bytes\n", raw_data, raw_size);
    CVI_LOGI("LSC alloc raw_unpack before: count=%u elem=%zu size=%u bytes\n",
        calib_width * calib_height, sizeof(uint16_t), unpack_size);
    raw_data_unpack = (uint16_t*)calloc(calib_width * calib_height, sizeof(uint16_t));
    CVI_LOGI("LSC alloc raw_unpack after: ptr=%p size=%u bytes\n", raw_data_unpack, unpack_size);
    if (raw_data == NULL || raw_data_unpack == NULL) {
        CVI_LOGE("LSC alloc buffers failed: raw_data=%p raw_unpack=%p\n", raw_data, raw_data_unpack);
        ret = MAPI_ERR_NOMEM;
        goto EXIT_CALIB;
    }

    ret = _MAPI_VCAP_GetRawData(vi_pipe, raw_data, raw_size, height, &stride);
    if (ret != MAPI_SUCCESS)
        goto EXIT_CALIB;

    ret = _MAPI_VCAP_UnpackRaw(raw_data, raw_data_unpack, width, height,
        calib_width, calib_height, stride, raw_pack_mode);
    if (ret != MAPI_SUCCESS)
        goto EXIT_CALIB;
    MAPI_FREE_PTR(raw_data);
    CVI_LOGI("LSC free raw_data buffer: %u bytes\n", raw_size);

    if (CVI_ISP_GetBlackLevelAttr(vi_pipe, &blc_attr) != CVI_SUCCESS || CVI_ISP_GetMeshShadingAttr(vi_pipe, &mlsc_attr) != CVI_SUCCESS || CVI_ISP_GetMeshShadingGainLutAttr(vi_pipe, &mlsc_gain_lut_attr) != CVI_SUCCESS || CVI_ISP_GetRadialShadingAttr(vi_pipe, &rlsc_attr) != CVI_SUCCESS || CVI_ISP_GetRadialShadingGainLutAttr(vi_pipe, &rlsc_gain_lut_attr) != CVI_SUCCESS) {
        ret = MAPI_ERR_FAILURE;
        goto EXIT_CALIB;
    }

    gain_size = (CVI_U32)(CVI_ISP_LSC_GRID_POINTS * 3 * (int)sizeof(int) + ISP_RLSC_WINDOW_SIZE * 4 * (int)sizeof(int));
    CVI_LOGI("LSC alloc gain buffers start: total=%u bytes r=%zu g=%zu b=%zu radius=%zu\n",
        gain_size, CVI_ISP_LSC_GRID_POINTS * sizeof(int),
        CVI_ISP_LSC_GRID_POINTS * sizeof(int),
        CVI_ISP_LSC_GRID_POINTS * sizeof(int),
        ISP_RLSC_WINDOW_SIZE * 4 * sizeof(int));
    CVI_LOGI("LSC alloc lsc_r_gain before: count=%d elem=%zu size=%zu bytes\n",
        CVI_ISP_LSC_GRID_POINTS, sizeof(int), CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    lsc_r_gain = (int*)calloc(CVI_ISP_LSC_GRID_POINTS, sizeof(int));
    CVI_LOGI("LSC alloc lsc_r_gain after: ptr=%p size=%zu bytes\n",
        lsc_r_gain, CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    CVI_LOGI("LSC alloc lsc_g_gain before: count=%d elem=%zu size=%zu bytes\n",
        CVI_ISP_LSC_GRID_POINTS, sizeof(int), CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    lsc_g_gain = (int*)calloc(CVI_ISP_LSC_GRID_POINTS, sizeof(int));
    CVI_LOGI("LSC alloc lsc_g_gain after: ptr=%p size=%zu bytes\n",
        lsc_g_gain, CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    CVI_LOGI("LSC alloc lsc_b_gain before: count=%d elem=%zu size=%zu bytes\n",
        CVI_ISP_LSC_GRID_POINTS, sizeof(int), CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    lsc_b_gain = (int*)calloc(CVI_ISP_LSC_GRID_POINTS, sizeof(int));
    CVI_LOGI("LSC alloc lsc_b_gain after: ptr=%p size=%zu bytes\n",
        lsc_b_gain, CVI_ISP_LSC_GRID_POINTS * sizeof(int));
    CVI_LOGI("LSC alloc lsc_radius_gain before: count=%d elem=%zu size=%zu bytes\n",
        ISP_RLSC_WINDOW_SIZE * 4, sizeof(int), ISP_RLSC_WINDOW_SIZE * 4 * sizeof(int));
    lsc_radius_gain = (int*)calloc(ISP_RLSC_WINDOW_SIZE * 4, sizeof(int));
    CVI_LOGI("LSC alloc lsc_radius_gain after: ptr=%p size=%zu bytes\n",
        lsc_radius_gain, ISP_RLSC_WINDOW_SIZE * 4 * sizeof(int));
    if (!lsc_r_gain || !lsc_g_gain || !lsc_b_gain || !lsc_radius_gain) {
        CVI_LOGE("LSC alloc gain buffers failed: r=%p g=%p b=%p radius=%p\n",
            lsc_r_gain, lsc_g_gain, lsc_b_gain, lsc_radius_gain);
        ret = MAPI_ERR_NOMEM;
        goto EXIT_CALIB;
    }

    if (mlsc_attr.MeshCorrectMode == MESH_CHROMA_MODE) {
        isp_algo_lsc_calibration(raw_data_unpack, calib_width, calib_height, bayer_id,
            CVI_ISP_LSC_GRID_COL, CVI_ISP_LSC_GRID_ROW, 1, 0,
            blc_attr.stAuto.OffsetR[0], blc_attr.stAuto.OffsetGr[0],
            blc_attr.stAuto.OffsetGb[0], blc_attr.stAuto.OffsetB[0],
            &center_x, &center_y, &radius, &norm,
            lsc_r_gain, lsc_g_gain, lsc_b_gain, lsc_radius_gain);
        _MAPI_VCAP_UpdateMlscGainLutAttr(&mlsc_gain_lut_attr, color_temp, lsc_r_gain, lsc_g_gain, lsc_b_gain);
        isp_algo_lsc_calibration(raw_data_unpack, calib_width, calib_height, bayer_id,
            CVI_ISP_LSC_GRID_COL, CVI_ISP_LSC_GRID_ROW, 2, 0,
            blc_attr.stAuto.OffsetR[0], blc_attr.stAuto.OffsetGr[0],
            blc_attr.stAuto.OffsetGb[0], blc_attr.stAuto.OffsetB[0],
            &center_x, &center_y, &radius, &norm,
            lsc_r_gain, lsc_g_gain, lsc_b_gain, lsc_radius_gain);
    } else {
        isp_algo_lsc_calibration(raw_data_unpack, calib_width, calib_height, bayer_id,
            CVI_ISP_LSC_GRID_COL, CVI_ISP_LSC_GRID_ROW, 0, 0,
            blc_attr.stAuto.OffsetR[0], blc_attr.stAuto.OffsetGr[0],
            blc_attr.stAuto.OffsetGb[0], blc_attr.stAuto.OffsetB[0],
            &center_x, &center_y, &radius, &norm,
            lsc_r_gain, lsc_g_gain, lsc_b_gain, lsc_radius_gain);
        _MAPI_VCAP_UpdateMlscGainLutAttr(&mlsc_gain_lut_attr, color_temp, lsc_r_gain, lsc_g_gain, lsc_b_gain);
    }
    MAPI_FREE_PTR(raw_data_unpack);
    CVI_LOGI("LSC free raw_unpack buffer: %u bytes\n", unpack_size);

    if (calib_width != width || calib_height != height) {
        CVI_LOGI("LSC scale calibration geometry: center=(%d,%d) radius=%d norm=%d calib=%ux%u src=%ux%u\n",
            center_x, center_y, radius, norm, calib_width, calib_height, width, height);
        center_x = (center_x * (int)width + (int)calib_width / 2) / (int)calib_width;
        center_y = (center_y * (int)height + (int)calib_height / 2) / (int)calib_height;
        radius = (radius * (int)width + (int)calib_width / 2) / (int)calib_width;
        norm = (norm * (int)calib_width + (int)width / 2) / (int)width;
        CVI_LOGI("LSC scaled calibration geometry: center=(%d,%d) radius=%d norm=%d\n",
            center_x, center_y, radius, norm);
    }

    if (mlsc_attr.MeshCorrectMode == MESH_CHROMA_MODE) {
        int color_tmp_num = mlsc_gain_lut_attr.Size;
        if (rlsc_attr.RadiusScaleRGB == 0) {
            rlsc_attr.RadiusScaleRGB = norm;
            rlsc_attr.CenterX = center_x;
            rlsc_attr.CenterY = center_y;
        } else {
            rlsc_attr.RadiusScaleRGB = (rlsc_attr.RadiusScaleRGB * color_tmp_num + norm) / (color_tmp_num + 1);
            rlsc_attr.CenterX = (rlsc_attr.CenterX * color_tmp_num + center_x) / (color_tmp_num + 1);
            rlsc_attr.CenterY = (rlsc_attr.CenterY * color_tmp_num + center_y) / (color_tmp_num + 1);
        }
        for (int i = 0; i < ISP_RLSC_WINDOW_SIZE; ++i) {
            if (rlsc_gain_lut_attr.GGain[i] == 0) {
                rlsc_gain_lut_attr.GGain[i] = lsc_radius_gain[i];
            } else {
                rlsc_gain_lut_attr.GGain[i] = (rlsc_gain_lut_attr.GGain[i] * color_tmp_num + lsc_radius_gain[i]) / (color_tmp_num + 1);
            }
        }
        if (CVI_ISP_SetRadialShadingAttr(vi_pipe, &rlsc_attr) != CVI_SUCCESS || CVI_ISP_SetRadialShadingGainLutAttr(vi_pipe, &rlsc_gain_lut_attr) != CVI_SUCCESS) {
            ret = MAPI_ERR_FAILURE;
            goto EXIT_CALIB;
        }
    }

    if (CVI_ISP_SetMeshShadingAttr(vi_pipe, &mlsc_attr) != CVI_SUCCESS || CVI_ISP_SetMeshShadingGainLutAttr(vi_pipe, &mlsc_gain_lut_attr) != CVI_SUCCESS) {
        ret = MAPI_ERR_FAILURE;
        goto EXIT_CALIB;
    }

EXIT_CALIB:
    MAPI_FREE_PTR(raw_data);
    MAPI_FREE_PTR(raw_data_unpack);
    MAPI_FREE_PTR(lsc_r_gain);
    MAPI_FREE_PTR(lsc_g_gain);
    MAPI_FREE_PTR(lsc_b_gain);
    MAPI_FREE_PTR(lsc_radius_gain);
    if (ret == MAPI_SUCCESS) {
        CVI_LOGI("LSC calibrate done: success\n");
    } else {
        CVI_LOGE("LSC calibrate done: failed ret=%d\n", ret);
    }
    return ret;
}
