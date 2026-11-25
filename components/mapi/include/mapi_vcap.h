#ifndef __MAPI_VCAP_H__
#define __MAPI_VCAP_H__

#include "cvi_comm_cif.h"
#include "cvi_comm_isp.h"
#include "cvi_comm_vi.h"
#include "stdbool.h"
#include "stddef.h"
#include "cvi_ae.h"
#include "cvi_awb.h"
#include "cvi_isp.h"
#include "stdint.h"
#include "mapi_define.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef MAPI_HANDLE_T MAPI_VCAP_SENSOR_HANDLE_T;

typedef struct MAPI_VCAP_SENSOR_ATTR_S {
    uint8_t     u8SnsId;
    uint8_t     u8WdrMode; //0:linear, 3:wdr 2to1
    uint8_t     u8I2cBusId;
    uint8_t     u8I2cSlaveAddr; //0xFF means used default slave addr.
    uint8_t     u8HwSync;
    uint8_t     u8MipiDev; //0xFF means used default LaneId&PNSwap.
    uint8_t     u8CamClkId;
    uint8_t     u8RstGpioInx;
    uint8_t     u8RstGpioPin;
    uint8_t     u8RstGpioPol;
    bool        bMclkEn;
    bool        bHsettlen;
    uint8_t     u8Hsettle;
    uint8_t     u8Orien;
    int8_t      as8LaneId[5];
    int8_t      as8PNSwap[5];
    int8_t      as8FuncId[TTL_PIN_FUNC_NUM];
    int8_t      aPipe[VI_MAX_PIPE_NUM];
    uint32_t    u32sensortype;
    uint32_t    u32LaneSwitchPin;
    uint32_t    u32LaneSwitchPinPol;
} MAPI_VCAP_SENSOR_ATTR_T;

typedef struct MAPI_VCAP_CHN_ATTR_S {
    uint32_t            u32Width;
    uint32_t            u32Height;
    PIXEL_FORMAT_E      enPixelFmt;
    COMPRESS_MODE_E     enCompressMode;
    bool                bMirror;
    bool                bFlip;
    float               f32Fps;
    uint32_t            vbcnt;
#ifdef __CV184X__
    VI_ISP_YUV_SCENE_E  scenemode;
#endif
} MAPI_VCAP_CHN_ATTR_T;

typedef struct MAPI_VCAP_ATTR_S {
    uint8_t                     u8DevNum;
    MAPI_VCAP_SENSOR_ATTR_T attr_sns[VI_MAX_DEV_NUM];
    MAPI_VCAP_CHN_ATTR_T    attr_chn[VI_MAX_DEV_NUM];
} MAPI_VCAP_ATTR_T;

typedef struct MAPI_VCAP_RAW_DATA_S {
    void *pPrivateData;
    int32_t (*pfn_VCAP_RawDataProc)(uint32_t ViPipe, VIDEO_FRAME_INFO_S *pVCapRawData,
                                    uint32_t u32DataNum, void *pPrivateData);
} MAPI_VCAP_RAW_DATA_T;

typedef enum MAPI_VCAP_CMD_E
{
    MAPI_VCAP_CMD_Fps,
    MAPI_VCAP_CMD_Rotate,
    MAPI_VCAP_CMD_MirrorFlip,
    MAPI_VCAP_CMD_BUTT
}MAPI_VCAP_CMD_E;

typedef struct MAPI_VCAP_MIRRORFLIP_ATTR_S {
    bool bMirror;
    bool bFlip;
} MAPI_VCAP_MIRRORFLIP_ATTR_S;

typedef struct MAPI_VCAP_LSC_DATA_S {
    uint32_t lsc_valid;
    ISP_MESH_SHADING_ATTR_S mlsc_attr;
    ISP_MESH_SHADING_GAIN_LUT_ATTR_S mlsc_gain_lut_attr;
    ISP_RADIAL_SHADING_ATTR_S rlsc_attr;
    ISP_RADIAL_SHADING_GAIN_LUT_ATTR_S rlsc_gain_lut_attr;
} MAPI_VCAP_LSC_DATA_T;

int MAPI_VCAP_InitSensor(MAPI_VCAP_SENSOR_HANDLE_T* sns_hdl,
    int sns_id, MAPI_VCAP_ATTR_T* vcap_attr);
int MAPI_VCAP_DeinitSensor(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StartDev(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StopDev(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StartChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StopChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StartPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_StopPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_InitISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_DeInitISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_GetISP(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_CMD_E enCMD,
                            void *pAttr, uint32_t u32Len);
int MAPI_VCAP_GetAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_CMD_E enCMD,
                            void *pAttr, uint32_t u32Len);
int MAPI_VCAP_SetChnCropAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_CROP_INFO_S *pstCropInfo);
int MAPI_VCAP_GetChnCropAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_CROP_INFO_S *pstCropInfo);
int MAPI_VCAP_SetDumpRawAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_DUMP_ATTR_S *pstDumpAttr);
int MAPI_VCAP_GetDumpRawAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VI_DUMP_ATTR_S *pstDumpAttr);
int MAPI_VCAP_StartDumpRaw(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, uint32_t u32Count,
                               MAPI_VCAP_RAW_DATA_T *pstVCapRawData);
int MAPI_VCAP_StopDumpRaw(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_GetSensorPipeAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, int* status);
int MAPI_VCAP_GetSensorPipe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_GetSensorChn(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_AutoFocusOnce(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetAutoFocusEnable(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, bool enable, uint32_t poll_times);
int MAPI_VCAP_GetExposureAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_EXPOSURE_ATTR_S *pstExpAttr);
int MAPI_VCAP_SetExposureAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_EXPOSURE_ATTR_S *pstExpAttr);
int MAPI_VCAP_GetAntiFlicker(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, CVI_U8 *pu8Enable, CVI_U8 *pu8Frequency);
int MAPI_VCAP_SetAntiFlicker(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, CVI_U8 u8Enable, CVI_U8 u8Frequency);
int MAPI_VCAP_GetAWBAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_AWB_ATTR_EX_S *pstAWBAttrEx);
int MAPI_VCAP_SetAWBAttrEx(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_AWB_ATTR_EX_S *pstAWBAttrEx);
int MAPI_VCAP_GetCAAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_CA_ATTR_S *pstCAAttr);
int MAPI_VCAP_SetCAAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, ISP_CA_ATTR_S *pstCAAttr);
int MAPI_VCAP_GetFrame(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VIDEO_FRAME_INFO_S* frame);
// Deprecated, use MAPI_ReleaseFrame instead
int MAPI_VCAP_ReleaseFrame(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, VIDEO_FRAME_INFO_S* frame);
int MAPI_VCAP_GetGeneralVcapAttr(MAPI_VCAP_ATTR_T* vcap_attr, uint8_t u8DevNum);
int MAPI_VCAP_GetSnsAttrFromFile(bool *dual_sns, MAPI_VCAP_ATTR_T *attr);
int MAPI_VCAP_InitSensorFromFile(MAPI_VCAP_SENSOR_HANDLE_T* sns_hdl, int sns_id, MAPI_VCAP_ATTR_T* attr);
int MAPI_VCAP_SetPqBinPath(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetEffect(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, char* effect_bin_path);
int MAPI_VCAP_GetLscData(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, MAPI_VCAP_LSC_DATA_T* lsc_data);
int MAPI_VCAP_SetLscData(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, const MAPI_VCAP_LSC_DATA_T* lsc_data);
int MAPI_VCAP_CalibrateLsc(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl, int color_temp);
#ifdef DUAL_OS
int MAPI_VCAP_InitSensorDetect(int sns_id, void* cb);
int MAPI_VCAP_SetAhdMode(int sns_id, int mode);
int MAPI_VCAP_GetAhdMode(int sns_id, int *mode, int *status, int8_t sensortype);
#endif
#ifdef CHIP_184X
int MAPI_VCAP_SetAhdInit(int sns_id, bool isFirstInit);
int MAPI_VCAP_StartMipi(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetSnsInit(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_MipiReset(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetMipiAttr(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetSensorClock(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetSensorReset(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
int MAPI_VCAP_SetSnsProbe(MAPI_VCAP_SENSOR_HANDLE_T sns_hdl);
#endif
#ifdef __cplusplus
}
#endif

#endif
