#include "gaugemng.h"
#include "cvi_log.h"
#include "osal.h"
#include "sysutils_eventhub.h"
#include "usb.h"
#include "mode.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

static bool s_bGAUGEMNGInitState = 0;
static pthread_mutex_t s_GAUGEMNGMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_GAUGEMNGCheckId;
static bool s_bGAUGEMNGCheckRun;

/** macro define */
#define GAUGEMNG_CHECK_INTERVAL (1) /**< gauge check interval, unit s */
#define GAUGEMNG_LOWLEVEL_RESET (20) /**< gauge level interval, percent */

/** sysfs path for kernel adc-simple-battery driver */
#define BATTERY_SYSFS_CAPACITY "/sys/class/power_supply/battery-adc/capacity"

/** 闪光灯 GPIO sysfs（DC309: gpio352），高电平=闪光灯点亮（仅拍照/录像拍摄时拉高，等效拍照模式判断） */
#define FLASH_LED_GPIO_SYSFS "/sys/class/gpio/gpio352/value"

/** gaugemng information */
typedef struct tagGAUGEMNG_INFO_S {
    int32_t s32Level;
    int32_t s32LastLevel;
    int32_t last_index;
    bool bLowLevelState;
    bool bUltraLowLevelState;
    bool bCharge;
    bool bLastCharge;
    GAUGEMNG_CFG_S stCfg;
} GAUGEMNG_INFO_S;

static GAUGEMNG_INFO_S s_stGAUGEMNGInfo;

static int32_t GAUGEMNG_InParmValidChck(const GAUGEMNG_CFG_S* stCfg)
{
    /** parm check */
    if (NULL == stCfg) {
        CVI_LOGE("GAUGEMNG_InParmValidChck fail\n");
        return -1;
    }

    if (stCfg->s32LowLevel > 100 || stCfg->s32UltraLowLevel < 0 || stCfg->s32UltraLowLevel > stCfg->s32LowLevel) {
        CVI_LOGE("stCfg parm error\n");
        return -1;
    }
    return 0;
}

static int32_t GAUGEMNG_InternalParmInit(const GAUGEMNG_CFG_S* pstCfg)
{
    if (NULL == pstCfg) {
        CVI_LOGE("GAUGEMNG_InParmValidChck fail\n");
        return -1;
    }

    s_stGAUGEMNGInfo.s32Level = 100;
    s_stGAUGEMNGInfo.s32LastLevel = 0;
    s_stGAUGEMNGInfo.last_index = 0;
    s_stGAUGEMNGInfo.bLowLevelState = 0;
    s_stGAUGEMNGInfo.bUltraLowLevelState = 0;

    memcpy(&s_stGAUGEMNGInfo.stCfg, pstCfg, sizeof(GAUGEMNG_CFG_S));

    return 0;
}

static void GAUGEMNG_UltraLowLevelCheck(GAUGEMNG_INFO_S* pstGaugemngInfo)
{
    EVENT_S stEvent;
    if (0 == pstGaugemngInfo->bUltraLowLevelState) {
        if (pstGaugemngInfo->s32Level <= pstGaugemngInfo->stCfg.s32UltraLowLevel && pstGaugemngInfo->bCharge == 0) {
            memset(&stEvent, '\0', sizeof(stEvent));
            stEvent.arg1 = pstGaugemngInfo->s32Level;
            stEvent.topic = EVENT_GAUGEMNG_LEVEL_ULTRALOW;
            EVENTHUB_Publish(&stEvent);
            CVI_LOGW("battery level ultralow event\n");
            pstGaugemngInfo->bUltraLowLevelState = 1;
        }
    }
    return;
}

static void GAUGEMNG_LowLevelCheck(GAUGEMNG_INFO_S* pstGaugemngInfo)
{
	EVENT_S stEvent;

	// CVI_LOGW("s32LowLevel = %d, s32UltraLowLevel = %d\n", pstGaugemngInfo->stCfg.s32LowLevel, pstGaugemngInfo->stCfg.s32UltraLowLevel);
	if (0 == pstGaugemngInfo->bLowLevelState) {
		if (pstGaugemngInfo->s32Level <= pstGaugemngInfo->stCfg.s32LowLevel && pstGaugemngInfo->bCharge == 0) {
			memset(&stEvent, '\0', sizeof(stEvent));
			stEvent.topic = EVENT_GAUGEMNG_LEVEL_LOW;
			stEvent.arg1 = pstGaugemngInfo->s32Level;
			EVENTHUB_Publish(&stEvent);
			CVI_LOGW("battery level low event\n");
			// pstGaugemngInfo->bLowLevelState = 1;
		}
	} else {
		if (pstGaugemngInfo->s32Level >= (pstGaugemngInfo->stCfg.s32LowLevel + GAUGEMNG_LOWLEVEL_RESET)) {
			memset(&stEvent, '\0', sizeof(stEvent));
			stEvent.topic = EVENT_GAUGEMNG_LEVEL_NORMAL;
			stEvent.arg1 = pstGaugemngInfo->s32Level;
			EVENTHUB_Publish(&stEvent);
			CVI_LOGW("battery level restore normal\n");

			pstGaugemngInfo->bLowLevelState = 0;
                        pstGaugemngInfo->bUltraLowLevelState = 0;
                }
        }
        return;
}

#define USB_VBUS_PATH "/proc/cviusb/vbus"

static int32_t get_usb_charging_by_io(uint32_t* value)
{
    FILE* fp;
    char buf[16] = { 0 };

    fp = fopen(USB_VBUS_PATH, "r");
    if (!fp) {
        CVI_LOGI("open %s fail\n", USB_VBUS_PATH);
        return -1;
    }

    if (fgets(buf, sizeof(buf), fp) == NULL) {
        CVI_LOGI("read %s fail\n", USB_VBUS_PATH);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    if (strncmp(buf, "high", 4) == 0) {
        *value = USB_STATE_INSERT;
    } else {
        *value = USB_STATE_OUT;
    }
    return 0;
}

static void GAUGEMNG_ChargeStateChangeCheck(GAUGEMNG_INFO_S* pstGaugemngInfo)
{
	EVENT_S stEvent;

	if (NULL == pstGaugemngInfo) {
		CVI_LOGE("error the charge state\n");
		return;
	}

	if (pstGaugemngInfo->bCharge != pstGaugemngInfo->bLastCharge) {
		memset(&stEvent, '\0', sizeof(stEvent));
		stEvent.topic = EVENT_GAUGEMNG_CHARGESTATE_CHANGE;
		stEvent.arg1 = pstGaugemngInfo->bCharge;
		EVENTHUB_Publish(&stEvent);
		CVI_LOGW("battery chargestate changge event\n");
	}
	return;
}

int32_t GAUGENBG_IdexChangeCheck(GAUGEMNG_INFO_S *pstGaugemngInfo)
{

	int32_t index = 0;
        int32_t s32Level = pstGaugemngInfo->s32Level;

        if (pstGaugemngInfo->bCharge == 1) {
            index = 3;
        } else {
            if (s32Level > 72) {
                index = 0;
            } else if (s32Level > 46 && s32Level <= 72) {
                index = 1;
            } else if (s32Level > 28 && s32Level <= 46) {
                index = 2;
            }
        }

        return index;
}

static void GAUGEMNG_LevelChangeCheck(GAUGEMNG_INFO_S *pstGaugemngInfo)
{
	EVENT_S stEvent;
	int32_t index = 0;
	bool bUpdate = false;

	index = GAUGENBG_IdexChangeCheck(pstGaugemngInfo);

	if (index != pstGaugemngInfo->last_index) {
		if (pstGaugemngInfo->bCharge) {
			/* 充电中：跟随显示充电状态 */
			bUpdate = true;
		} else if (pstGaugemngInfo->bLastCharge) {
			/* 刚拔出充电器：立即同步实际档位，避免停留在充电图标 */
			bUpdate = true;
		} else if (index > pstGaugemngInfo->last_index) {
			/* 档位下降(电量减少)：允许更新 */
			bUpdate = true;
		}
		/* 档位回升(如退出拍照模式后电压回升)：不更新，保持原档位，避免回跳 */
	}

	if (bUpdate) {
		pstGaugemngInfo->last_index = index;
		memset(&stEvent, '\0', sizeof(stEvent));
		stEvent.topic = EVENT_GAUGEMNG_LEVEL_CHANGE;
		stEvent.arg1 = index;
		EVENTHUB_Publish(&stEvent);
	}
	return;
}

static int32_t GAUGEMNG_ChargeStateCheck(GAUGEMNG_INFO_S *pstGaugemngInfo)
{
	bool bCharge = 0;
	if (NULL == pstGaugemngInfo) {
		CVI_LOGE("error the charge state\n");
		return -1;
	}

	uint32_t charge = 0;
        get_usb_charging_by_io(&charge);
        // CVI_LOGE("charge = %d\n", charge);
        if (USB_STATE_INSERT == charge) {
            bCharge = 1;
        } else if (USB_STATE_OUT == charge) {
            bCharge = 0;
            // OSAL_FS_System("poweroff -f");
        }
        if (pstGaugemngInfo->bCharge != bCharge) {
            CVI_LOGI("charge state changed: %d -> %d\n", pstGaugemngInfo->bCharge, bCharge);
            pstGaugemngInfo->bCharge = bCharge;
            GAUGEMNG_LevelChangeCheck(pstGaugemngInfo);
            // GAUGEMNG_ChargeStateChangeCheck(pstGaugemngInfo);
            pstGaugemngInfo->bLastCharge = bCharge;
        }
        return 0;
}

/* 读取闪光灯 GPIO 电平：1=闪光灯点亮中，0=未点亮 */
static int32_t _GAUGEMNG_IsFlashOn(void)
{
    FILE* fp;
    char buf[4] = {0};

    fp = fopen(FLASH_LED_GPIO_SYSFS, "r");
    if (!fp) {
        return 0;
    }
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    return (buf[0] == '1');
}

/*get battery capacity percentage from kernel adc-simple-battery driver via sysfs*/
int32_t GAUGEMNG_GetPercentage(void)
{
    FILE* fp;
    int32_t capacity = -1;

    /* 拍照/录像闪光灯点亮期间，负载电流大、电池电压瞬态跌落，
     * 此时采样会误判电量等级：跳过本次获取，闪光结束后恢复正常采样 */
    if (MODEMNG_GetCurWorkMode() == WORK_MODE_PHOTO && _GAUGEMNG_IsFlashOn()) {
        CVI_LOGD("flash led on, skip battery capacity sampling\n");
        return -1;
    }

    fp = fopen(BATTERY_SYSFS_CAPACITY, "r");
    if (!fp) {
        CVI_LOGE("open battery sysfs fail: %s\n", BATTERY_SYSFS_CAPACITY);
        return -1;
    }

    if (fscanf(fp, "%d", &capacity) != 1) {
        CVI_LOGE("read battery capacity fail\n");
        fclose(fp);
        return -1;
    }

    fclose(fp);

    if (capacity < 0 || capacity > 100) {
        CVI_LOGE("invalid battery capacity: %d\n", capacity);
        return -1;
    }

    CVI_LOGD("battery capacity from sysfs: %d%%\n", capacity);
    return capacity;
}

static int32_t GAUGEMNG_LevelCheck(GAUGEMNG_INFO_S* pstGaugemngInfo)
{
    int32_t s32Level = 0;

    if (NULL == pstGaugemngInfo) {
        CVI_LOGE("error the charge state\n");
        return -1;
    }

    s32Level = GAUGEMNG_GetPercentage();
    if (-1 == s32Level || s32Level > 100) {
        CVI_LOGE("get battery percentage fail, ret=%d\n", s32Level);
        return -1;
    }

    CVI_LOGD("battery level: cur=%d last=%d charge=%d\n",
        s32Level, pstGaugemngInfo->s32Level, pstGaugemngInfo->bCharge);

    GAUGEMNG_ChargeStateCheck(pstGaugemngInfo);
    GAUGEMNG_LowLevelCheck(pstGaugemngInfo);

    if (pstGaugemngInfo->s32Level != s32Level) {
        // CVI_LOGI("battery level changed: %d%% -> %d%% (charge=%d)\n",
        //     pstGaugemngInfo->s32Level, s32Level, pstGaugemngInfo->bCharge);
        pstGaugemngInfo->s32Level = s32Level;
        GAUGEMNG_LevelChangeCheck(pstGaugemngInfo);
        GAUGEMNG_UltraLowLevelCheck(pstGaugemngInfo);
        pstGaugemngInfo->s32LastLevel = s32Level;
    }
    return 0;
}

static void* GAUGEMNG_LevelCheckThread(void* pData)
{
    /** Set Task Name */
    prctl(PR_SET_NAME, "GAUGE_LEVEL_CHECK", 0, 0, 0);

    while (s_bGAUGEMNGCheckRun) {
        GAUGEMNG_LevelCheck(&s_stGAUGEMNGInfo);
        // GAUGEMNG_ChargeStateCheck(&s_stGAUGEMNGInfo);
        usleep(4 * 1000 * 1000);
    }

    CVI_LOGW("gauge check thread exit\n");
    return NULL;
}

int32_t GAUGEMNG_RegisterEvent(void)
{
	int32_t s32Ret = 0;
	s32Ret = EVENTHUB_RegisterTopic(EVENT_GAUGEMNG_LEVEL_CHANGE);
	if (0 != s32Ret) {
		CVI_LOGE("Register battery level change event fail\n");
		return -1;
	}
	s32Ret = EVENTHUB_RegisterTopic(EVENT_GAUGEMNG_LEVEL_LOW);
	if (0 != s32Ret) {
		CVI_LOGE("Register battery level low event fail\n");
		return -1;
	}
	s32Ret = EVENTHUB_RegisterTopic(EVENT_GAUGEMNG_LEVEL_ULTRALOW);
	if (0 != s32Ret) {
		CVI_LOGE("Register battery level ultra low event fail\n");
		return -1;
	}
	s32Ret = EVENTHUB_RegisterTopic(EVENT_GAUGEMNG_LEVEL_NORMAL);
	if (0 != s32Ret) {
		CVI_LOGE("Register battery level recover normal event fail\n");
		return -1;
	}
	s32Ret = EVENTHUB_RegisterTopic(EVENT_GAUGEMNG_CHARGESTATE_CHANGE);
	if (0 != s32Ret) {
		CVI_LOGE("Register battery level recover normal event fail\n");
		return -1;
	}
	return 0;
}

int32_t GAUGEMNG_Init(const GAUGEMNG_CFG_S *pstCfg)
{
	int32_t s32Ret;

	if (NULL == pstCfg) {
		CVI_LOGE("parm check error\n");
		return -1;
	}
	MUTEX_LOCK(s_GAUGEMNGMutex);

	if (1 == s_bGAUGEMNGInitState) {
		CVI_LOGE("already been started\n");
		MUTEX_UNLOCK(s_GAUGEMNGMutex);
		return -1;
        }

        s32Ret = GAUGEMNG_InParmValidChck(pstCfg);
        if (0 != s32Ret) {
            CVI_LOGE("GAUGEMNG_InParmValidChck Failed\n");
            MUTEX_UNLOCK(s_GAUGEMNGMutex);
            return -1;
        }

        GAUGEMNG_InternalParmInit(pstCfg);

        CVI_LOGI("gaugemng init: sysfs=%s low=%d%% ultralow=%d%% vbus=%s\n",
            BATTERY_SYSFS_CAPACITY,
            pstCfg->s32LowLevel,
            pstCfg->s32UltraLowLevel,
            USB_VBUS_PATH);

        /* Create gauge Check Task */
        s_bGAUGEMNGCheckRun = 1;
        s32Ret = pthread_create(&s_GAUGEMNGCheckId, NULL, GAUGEMNG_LevelCheckThread, NULL);
        if (0 != s32Ret) {
            CVI_LOGE("Create GaugeCheck Thread Fail!\n");
            MUTEX_UNLOCK(s_GAUGEMNGMutex);
            return -1;
        }

        s_bGAUGEMNGInitState = 1;
        MUTEX_UNLOCK(s_GAUGEMNGMutex);
        return 0;
}

int32_t GAUGEMNG_GetBatteryLevel(uint8_t* ps32Level)
{
	int32_t s32Ret = 0;
	if (NULL == ps32Level) {
		CVI_LOGE("error point value\n");
		return -1;
	}

	MUTEX_LOCK(s_GAUGEMNGMutex);
	if (false == s_bGAUGEMNGInitState) {
		CVI_LOGE("gaugemng no init\n");
		MUTEX_UNLOCK(s_GAUGEMNGMutex);
		return -1;
	}

	s32Ret = GAUGENBG_IdexChangeCheck(&s_stGAUGEMNGInfo);
	if (s32Ret < 0) {
		CVI_LOGE("CVI_HAL GAUGE GetLevel fail\n");
		return -1;
	}
	*ps32Level = s32Ret;
	MUTEX_UNLOCK(s_GAUGEMNGMutex);
	return 0;
}

int32_t GAUGEMNG_GetChargeState(bool *pbCharge)
{
	uint32_t charge = 0;

	if (NULL == pbCharge) {
		CVI_LOGE("pbCharge is null point\n");
		return -1;
	}

	MUTEX_LOCK(s_GAUGEMNGMutex);
	if (0 == s_bGAUGEMNGInitState) {
		CVI_LOGE("gaugemng no init\n");
		MUTEX_UNLOCK(s_GAUGEMNGMutex);
		return -1;
	}

	get_usb_charging_by_io(&charge);
	if (USB_STATE_INSERT == charge) {
		*pbCharge = 1;
	} else {
		*pbCharge = 0;
	}
	MUTEX_UNLOCK(s_GAUGEMNGMutex);
	return 0;
}

int32_t GAUGEMNG_DeInit(void)
{
	int32_t s32Ret;
	MUTEX_LOCK(s_GAUGEMNGMutex);
	/** Destory gauge Check Task */
	if (0 == s_bGAUGEMNGInitState) {
		CVI_LOGE("gaugemng no init\n");
		MUTEX_UNLOCK(s_GAUGEMNGMutex);
		return -1;
        }
        s_bGAUGEMNGCheckRun = 0;
        s32Ret = pthread_join(s_GAUGEMNGCheckId, NULL);
        if (0 != s32Ret) {
            CVI_LOGE("Join GaugeCheck Thread Fail!\n");
            MUTEX_UNLOCK(s_GAUGEMNGMutex);
            return -1;
        }

        s_bGAUGEMNGInitState = 0;

        MUTEX_UNLOCK(s_GAUGEMNGMutex);
        return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* #ifdef __cplusplus */
