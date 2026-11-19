#ifndef __CAMINFOMNG_H__
#define __CAMINFOMNG_H__

#include "appcomm.h"
#include "cvi_comm_isp.h"
#include <stdint.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#define CAMINFOMNG_EINVAL APP_APPCOMM_ERR_ID(APP_MOD_SYSTEM, APP_EINVAL)
#define CAMINFOMNG_EINTER APP_APPCOMM_ERR_ID(APP_MOD_SYSTEM, APP_EINTER)
#define CAMINFOMNG_ENODATA APP_APPCOMM_ERR_ID(APP_MOD_SYSTEM, APP_ERRNO_CUSTOM_BOTTOM)

typedef struct __cam_info_t {
    uint32_t FocusPos;
    uint32_t LscValid;
    ISP_MESH_SHADING_ATTR_S MlscAttr;
    ISP_MESH_SHADING_GAIN_LUT_ATTR_S MlscGainLutAttr;
    ISP_RADIAL_SHADING_ATTR_S RlscAttr;
    ISP_RADIAL_SHADING_GAIN_LUT_ATTR_S RlscGainLutAttr;
} cam_info_t;

int32_t CAMINFOMNG_GetInfo(cam_info_t* pstInfo);
int32_t CAMINFOMNG_UpdateInfo(const cam_info_t* pstInfo);
int32_t CAMINFOMNG_GetLscInfo(cam_info_t* pstInfo);
int32_t CAMINFOMNG_UpdateLscInfo(const cam_info_t* pstInfo);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* __CAMINFOMNG_H__ */
