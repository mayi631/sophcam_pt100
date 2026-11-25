#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "hal_wifi.h"
#include "hal_wifi_common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define HAL_WIFI_HOSTAPD_CONFIG_DIR "/dev/wifi/hostapd"
#define HAL_WIFI_HOSTAPD_CONFIG_FILE "/dev/wifi/hostapd/hostapd.conf"
#define HAL_WIFI_HOSTAPD_EXECUTE_FILE "hostapd"
#define HAL_WIFI_DHCPD_EXECUTE_FILE "/usr/sbin/udhcpd"
#define HAL_WIFI_DHCPD_CONFIG_FILE "/etc/network/udhcpd.conf"

static int32_t HAL_WIFI_AP_UpdateConfig(char* ifname, const HAL_WIFI_APMODE_CFG_S* pstApCfg, const char* pszConfigFile)
{
    int32_t s32Ret = 0;
    DIR* pDir;

    /** ensure /dev/wifi exist */
    pDir = opendir(HAL_WIFI_DEV_DIR);
    if (!pDir) {
        if (mkdir(HAL_WIFI_DEV_DIR, 0666) < 0) {
            printf("WiFi: Create '%s' fail,errno(%d)", HAL_WIFI_DEV_DIR, errno);
            return -1;
        }
    } else {
        closedir(pDir);
    }
    /** ensure hostapd configure file directory exist */
    pDir = opendir(HAL_WIFI_HOSTAPD_CONFIG_DIR);
    if (!pDir) {
        if (mkdir(HAL_WIFI_HOSTAPD_CONFIG_DIR, 0666) < 0) {
            printf("WiFi: Create '%s' fail,errno(%d)", HAL_WIFI_HOSTAPD_CONFIG_DIR, errno);
            return -1;
        }
    } else {
        closedir(pDir);
    }
    s32Ret = WIFI_HAL_UpdateApConfig(ifname, pstApCfg, pszConfigFile);
    if (0 != s32Ret) {
        printf("hal update config  fail\n");
        return -1;
    }
    if (chmod(pszConfigFile, 0666) < 0) {
        printf("WiFi: Failed to change '%s' to 0666 ,errno(%d)\n", pszConfigFile, errno);
        (void)unlink(pszConfigFile);
        s32Ret = -1;
    }
    return s32Ret;
}

int32_t HAL_WIFI_AP_HostapdStart()
{
    int32_t s32Ret;

    char cmd[192] = { '\0' };
    snprintf(cmd, sizeof(cmd), "%s -B %s", HAL_WIFI_HOSTAPD_EXECUTE_FILE, HAL_WIFI_HOSTAPD_CONFIG_FILE);
    s32Ret = system(cmd);
    if (s32Ret == -1) {
        if (ECHILD != errno) {
            printf("system errno[%d]\n", errno);
        }
    }

    return s32Ret;
}

int32_t HAL_WIFI_AP_DhcpdStart()
{
    int32_t s32Ret;

    char cmd[128] = { '\0' };
    snprintf(cmd, sizeof(cmd), "%s %s", HAL_WIFI_DHCPD_EXECUTE_FILE, HAL_WIFI_DHCPD_CONFIG_FILE);
    s32Ret = system(cmd);
    if (s32Ret == -1) {
        if (ECHILD != errno) {
            printf("system errno[%d]\n", errno);
        }
    }

    return s32Ret;
}

int32_t HAL_WIFI_AP_DhcpdStop()
{
    int32_t s32Ret;

    char cmd[128] = { '\0' };
    snprintf(cmd, sizeof(cmd), "%s %s", HAL_WIFI_KILL_EXECUTE_FILE, "udhcpd");
    sighandler_t SignalPrev;
    SignalPrev = signal(SIGCHLD, SIG_DFL);
    s32Ret = system(cmd);
    if (s32Ret == -1) {
        printf("system errno[%d]\n", errno);
    }
    signal(SIGCHLD, SignalPrev);

    return s32Ret;
}

int32_t HAL_WIFI_AP_HostapdStop()
{
    int32_t s32Ret;

    char cmd[128] = { '\0' };
    snprintf(cmd, sizeof(cmd), "%s %s", HAL_WIFI_KILL_EXECUTE_FILE, "hostapd");
    sighandler_t SignalPrev;
    SignalPrev = signal(SIGCHLD, SIG_DFL);
    s32Ret = system(cmd);
    if (s32Ret == -1) {
        printf("system errno[%d]\n", errno);
    }
    signal(SIGCHLD, SignalPrev);

    return s32Ret;
}

int32_t WIFI_AP_Start(char* ifname, const HAL_WIFI_APMODE_CFG_S* pstApCfg)
{
    int32_t s32Ret = 0;
    if ((NULL == ifname) || (*ifname == '\0') || (pstApCfg == NULL)) {
        printf("ifname is null or no content or stApCfg is null,error");
        return -1;
    }
    s32Ret = HAL_WIFI_AP_UpdateConfig(ifname, pstApCfg, HAL_WIFI_HOSTAPD_CONFIG_FILE);
    if (0 != s32Ret) {
        printf("update config error\n");
        return -1;
    }
    WIFI_HAL_PowerOnReset();

    /* 停掉 STA 相关的进程(wpa_supplicant / wifi-daemon / dhcpcd)，
     * 避免它们把 wlan0 当作 STA 持续连接路由器，与 AP 热点冲突 */
    {
        char szCmd[192] = { '\0' };
        snprintf(szCmd, sizeof(szCmd), "%s %s %s %s", HAL_WIFI_KILL_EXECUTE_FILE,
                 "wpa_supplicant", "wifi-daemon", "dhcpcd");
        (void)system(szCmd);
        snprintf(szCmd, sizeof(szCmd), "%s %s", HAL_WIFI_KILL_EXECUTE_FILE, "aicam_wifi");
        (void)system(szCmd);
    }

    /* 关键：先卸载再重新加载驱动，使驱动从干净的 AP 状态启动。
     * 若驱动被 STA(wpa_supplicant) 使用过后不重载，AP beacon 不会广播 */
    if (access("/sys/module/8189fs", F_OK) == 0) {
        char szCmd[192] = { '\0' };
        snprintf(szCmd, sizeof(szCmd), "%s %s", "rmmod", "8189fs");
        (void)system(szCmd);
        usleep(200 * 1000);
        snprintf(szCmd, sizeof(szCmd), "%s %s", "insmod", KOMOD_PATH "/3rd/8189fs.ko");
        (void)system(szCmd);
        usleep(1000 * 1000);
    }

    s32Ret = WIFI_UTILS_Ifconfig(HAL_WIFI_INTERFACE_NAME, 1);
    if (0 != s32Ret) {
        printf("ifconfig error\n");
    }

    s32Ret = WIFI_HAL_GetInterface(HAL_WIFI_INTERFACE_NAME);
    if (-1 == s32Ret) {
        printf("found no wlan0,get wifi interface fail\n");
        return -1;
    }

    s32Ret = WIFI_UTILS_SetIp(HAL_WIFI_INTERFACE_NAME, HAL_WIFI_IP);
    if (0 != s32Ret) {
        printf("ifconfig error\n");
        return -1;
    }
    s32Ret = WIFI_UTILS_AllFDClosexec();
    if (0 != s32Ret) {
        printf("All fd Closexec error\n");
        return -1;
    }
    s32Ret = HAL_WIFI_AP_HostapdStart();
    if (0 != s32Ret) {
        printf("Hostapd Start error\n");
        return -1;
    }

    s32Ret = HAL_WIFI_AP_DhcpdStart();
    if (0 != s32Ret) {
        HAL_WIFI_AP_HostapdStop();
        printf("Dhcpd Start error");
        return -1;
    }

    return 0;
}

int32_t WIFI_AP_Stop(void)
{
    int32_t s32Ret = 0;

    /* 先停止 hostapd（关键，停掉热点），即使 udhcpd 未运行也不阻断 */
    s32Ret = HAL_WIFI_AP_HostapdStop();
    if (s32Ret != 0) {
        printf("hostapd stop failed\n");
    }

    /* 再停止 udhcpd，失败不影响 hostapd 的停止结果 */
    (void)HAL_WIFI_AP_DhcpdStop();

    s32Ret = WIFI_UTILS_Ifconfig(HAL_WIFI_INTERFACE_NAME, 0);
    if (s32Ret != 0) {
        printf("ifconfig error");
        return -1;
    }

    /* 恢复 STA 联网相关的进程(wifi-daemon / wpa_supplicant / dhcpcd)，
     * 以便关闭热点后 WiFi 能回到联网模式 */
    {
        char szCmd[256] = { '\0' };
        /* 先恢复 dhcpcd */
        snprintf(szCmd, sizeof(szCmd), "%s start > /dev/null 2>&1 &", "/etc/init.d/S41dhcpcd");
        (void)system(szCmd);
        /* 再恢复 wpa_supplicant 和 wifi-daemon（参照 S99user 的启动方式） */
        snprintf(szCmd, sizeof(szCmd),
                 "wpa_supplicant -D nl80211 -i %s -c /etc/wpa_supplicant.conf -B > /dev/null 2>&1 &",
                 HAL_WIFI_INTERFACE_NAME);
        (void)system(szCmd);
        snprintf(szCmd, sizeof(szCmd), "wifi-daemon -l info > /dev/null 2>&1 &");
        (void)system(szCmd);
    }
    return 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
