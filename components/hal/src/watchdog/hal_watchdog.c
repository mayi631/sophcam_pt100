#include "hal_watchdog.h"
#include "cvi_log.h"
#include "osal.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/watchdog.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define HAL_FD_INITIALIZATION_VAL (-1)
#define HAL_WATCHDOG_DEV "/dev/watchdog"

#ifdef CHIP_184X
#define WDT_KO_PATH KOMOD_PATH "/cv184x_wdt.ko"
#else
#define WDT_KO_PATH KOMOD_PATH "/cv181x_wdt.ko"
#endif

static int32_t s_s32HALWATCHDOGfd = HAL_FD_INITIALIZATION_VAL;

int32_t HAL_WATCHDOG_Init(int32_t s32Time_s)
{
    int32_t s32Ret = 0;
    struct stat st;

    if (s_s32HALWATCHDOGfd != HAL_FD_INITIALIZATION_VAL) {
        CVI_LOGE("already init");
        return -1;
    }

    if (s32Time_s < 2 || s32Time_s > 1000) {
        CVI_LOGE("Interval time should not be less then two and bigger then 1000. %d\n", s32Time_s);
        return -1;
    }

    /* if build-in, no need to insmod */
    if (stat(HAL_WATCHDOG_DEV, &st) != 0) {
        // 不传递任何参数，只加载驱动
        s32Ret = OSAL_FS_Insmod(WDT_KO_PATH, NULL);
        if (0 != s32Ret) {
            CVI_LOGE("insmod wdt.ko: failed, errno(%d)\n", errno);
            return -1;
        }
    }

    s_s32HALWATCHDOGfd = open(HAL_WATCHDOG_DEV, O_RDWR);

    if (s_s32HALWATCHDOGfd < 0) {
        CVI_LOGE("open [%s] failed\n", HAL_WATCHDOG_DEV);
        return -1;
    }

    // 通过ioctl设置超时时间（秒）
    s32Ret = ioctl(s_s32HALWATCHDOGfd, WDIOC_SETTIMEOUT, &s32Time_s);
    if (-1 == s32Ret) {
        CVI_LOGE("WDIOC_SETTIMEOUT: failed, errno(%d)\n", errno);
        close(s_s32HALWATCHDOGfd);
        s_s32HALWATCHDOGfd = HAL_FD_INITIALIZATION_VAL;
        return -1;
    }

    // 喂狗，启动看门狗
    s32Ret = ioctl(s_s32HALWATCHDOGfd, WDIOC_KEEPALIVE);
    if (-1 == s32Ret) {
        CVI_LOGE("WDIOC_KEEPALIVE: failed, errno(%d)\n", errno);
        close(s_s32HALWATCHDOGfd);
        s_s32HALWATCHDOGfd = HAL_FD_INITIALIZATION_VAL;
        return -1;
    }

    CVI_LOGI("watchdog initialized, timeout=%ds, devname=%s\n", s32Time_s, HAL_WATCHDOG_DEV);

    return 0;
}

int32_t HAL_WATCHDOG_Feed(void)
{
    int32_t s32Ret = 0;
    s32Ret = ioctl(s_s32HALWATCHDOGfd, WDIOC_KEEPALIVE); /**feed dog */
    if (-1 == s32Ret) {
        CVI_LOGE("WDIOC_KEEPALIVE: failed, errno(%d)\n", errno);
        return -1;
    }

    return 0;
}

int32_t HAL_WATCHDOG_Deinit(void)
{
    int32_t s32Ret;

    if (s_s32HALWATCHDOGfd == HAL_FD_INITIALIZATION_VAL) {
        CVI_LOGE("watchdog not initialized,no need to close\n");
        return -1;
    }
    s32Ret = close(s_s32HALWATCHDOGfd);
    if (0 > s32Ret) {
        CVI_LOGE("wdrfd[%d] close,fail,errno(%d)\n", s_s32HALWATCHDOGfd, errno);
        return -1;
    }
    s_s32HALWATCHDOGfd = HAL_FD_INITIALIZATION_VAL;
    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
