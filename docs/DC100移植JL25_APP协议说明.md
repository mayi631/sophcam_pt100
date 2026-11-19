# DC100 移植 JL25 APP 协议（手机热点接入）说明

> 日期：2026-09-04
> 来源：`SDK_CV184X_DC309/sophcam_v20260623/applications/dashcam/ui/aicamera/dc309/src/protocol`（已联调闭环，见 `docs/dc309_协议接入APP说明_20260901.md`、`docs/交接说明_手机热点功能.md`、`docs/交接_协议联调进行中_20260831.md`）
> 目标：`SDK_CV184X_DC100/sophcam/applications/dashcam/ui/aicamera/dc100`
> 本文所有差异点均已逐项与 DC100 源码/库核对过，可直接照做。

> **同步说明（2026-09-08）**：DC309 已补齐协议 6.1.3/6.1.4/6.1.8 的实现说明。
> DC100 后续移植必须同步 `/app/getmediainfo` 的 `rectime:1`、6.1.4 的 SD 状态映射，
> 以及基于 `CLOCK_MONOTONIC` 的 `/app/getrecduration` 当前分段时长实现；本文末尾给出验收清单。

---

## 一、结论先行

1. **协议层本体（`src/protocol/` 全部 20 个文件）可以直接整体拷贝**，纯 socket 实现（HTTP:80 + TCP:8222 + TCP:6035 + UDP:49142 + 本地 RTSP:554），无 `system()` 调用，无 thttpd/netctrl 依赖。两块板子同样的芯片（CV1842CP）、同样的 defconfig 布局（`CONFIG_APPLICATION_NET` 未开、`CONFIG_SERVICES_SUBVIDEO=y`、`CONFIG_COMPONENTS_RTSP=y`、`CONFIG_ENABLE_SNAP` 未开、`CONFIG_WIFI_RTL8189=y`），协议依赖的所有头文件/库在 DC100 上全部存在。
2. **唯一不能直接搬的是 HAL 层热点修复**（`hal_wifi_ap.c`、`rtl8189fs/wifi_hal.c` 的三处踩坑修复）。DC309 的 HAL 有源码（`components/hal/src/wifi/`），DC100 的 HAL 是**预编译库**（`components/comps_install/`，独立 git 仓库 `sophcamd/comps_install`，分支 `dc100`，本地没有源码）。DC100 现有 `libcomp_hal.so` 仍是老逻辑：hostapd 配置生成用 `wpa=3` + `ieee80211n=1`，且没有 `rsn_pairwise=CCMP`，`WIFI_AP_Start` 里也**没有** killall/重载驱动的处理——这些正是 DC309 踩过的坑 2/坑 3。
3. **STA 管理架构不同**：DC309 的 STA 走 `wifi-daemon`（`/tmp/aicam_wifi.sock`），热点页通过 `wifi_manager_get_status()` 关 STA；**DC100 根本没有 wifi-daemon 二进制**（S99user 里那行启动是无效的），STA 由 UI 层 `src/hal_wifi/hal_wifi_ctrl.c` 直接控制 wpa_supplicant。所以热点页面代码不能原样拷，"关闭 STA 释放 wlan0"这一步要改调 DC100 自己的接口。

---

## 二、第一步：整体拷贝协议目录

把 DC309 的整个目录原样复制到 DC100 同级位置：

```bash
SRC=/home/mayi/SDK_CV184X_DC309/sophcam_v20260623/applications/dashcam/ui/aicamera/dc309/src/protocol
DST=/home/mayi/SDK_CV184X_DC100/sophcam/applications/dashcam/ui/aicamera/dc100/src/protocol

cp -r "$SRC" "$DST"
```

拷贝后的文件清单（20 个文件）：

```text
src/protocol/
├── jl25_app.c            旧版 APP /app/* JSON 接口（getfilelist/download/拍照/录像/分辨率设置）
├── jl25_appsock.c        旧版 APP TCP:6035 长连接 + 心跳 + 触发主动扫描
├── jl25_cmd.c            URL 总路由（/Misc /SetMode /Storage /Setup /Pin /app/* 旧CGI）
├── jl25_discovery.c      UDP:49142 广播/单播通知（触发 APP 建立 6035 连接）
├── jl25_discovery.h
├── jl25_events.c         订阅 EVENTHUB 事件 → {"Event":N} 推送到 8222
├── jl25_events.h
├── jl25_http.c           HTTP:80 + TCP:8222 服务主机、连接管理、JL25_Start/Stop
├── jl25_internal.h       模块内部共享声明
├── jl25_legacy.c         旧版 getwifi.cgi / Config.cgi / cammenu.xml 兼容
├── jl25_misc.c           /Misc 命令组（心跳/模式/关机/设备信息）
├── jl25_misc.h
├── jl25_preview.c        预览控制（自动切 WORK_MODE_MOVIE + 起/停 RTSP）
├── jl25_protocol.h       协议常量、事件编号、公共 API
├── jl25_setmode.c        /SetMode 命令组
├── jl25_setup.c          /Setup 命令组（快门=拍照/录像、格式化等）
├── jl25_storage.c        /Storage 命令组（文件列表/缩略图/断点下载/删除）
├── jl25_storage_guard.c  APP 连接后主动重扫（FILEMNG 状态翻转触发 DTCF_Scan）
└── preview_rtsp/
    ├── jl25_preview_rtsp.c  本地 RTSP server（rtsp://192.168.169.1:554/liveRTSP/av4）
    └── jl25_preview_rtsp.h
```

### 拷贝后即可直接编译通过的原因（已逐项核对）

| 协议依赖 | DC309 位置 | DC100 对应 | 状态 |
|---|---|---|---|
| `mode.h`（WORK_MODE_E、EVENT_MODEMNG_*、MODEMNG_*） | `dashcam/mode/include` | 同路径 | ✅ 事件枚举仅多一个 `EVENT_MODEMNG_PIP_TOGGLE`（dc100 独有，不影响）；`MODEMNG_SendMessage/GetCurWorkMode/GetInProgress/GetCardState/GetModeState` 全部存在 |
| `param.h`（PARAM_GetMenuParam/PARAM_MENU_VIDEO_SIZE/PHOTO_SIZE、PARAM_GetMediaComm/GetMediaMode、PARAM_GetWifiParam、PARAM_RECORD_CHN_ATTR_S.Subvideoen/SubBindVencId） | `dashcam/param/core/include` | 同路径 | ✅ 全部存在；`PARAM_MENU_S.VideoSize/PhotoSize` 结构一致 |
| `filemng.h`（FILEMNG_GetStorageStatus/SetStorageStatus、FILEMNG_STORAGE_STATE_*） | `applications/manager/include` | 同路径 | ✅ 签名一致 |
| `gaugemng.h`（GAUGEMNG_GetPercentage/GetChargeState） | 同上 | 同上 | ✅ |
| `appcomm.h`、`sysutils_eventhub.h`（EVENTHUB_*、EVENT_S、MESSAGE_S） | applications/include + comps_install/sysutils | 同路径 | ✅ `appcomm.h` 两边 md5 相同；EVENT_S 结构逐字节一致 |
| `subvideo_service.h`（VIDEO_SERVICR_CallbackSet/UnSet） | `app_services/include` | 同路径 | ✅ 两边 md5 相同 |
| `rtsp.h`（RTSP_Create/Destroy/SendFrame、RTSP_INFO_S、RTSP_MEDIA_INFO_S） | comps_install/rtsp | 同路径 | ✅ 两边 `rtsp.h` 完全相同 |
| `thumbnail_extractor.h`（含 `THUMBNAIL_EXTRACTOR_GetThumbnailByType`） | comps_install/thumbnail_extractor | 同路径 | ✅ `libcomp_thumbnail.so` 导出符号确认含 `GetThumbnailByType`；DC100 defconfig `CONFIG_COMPONENTS_THUMBNAIL_EXTRACTOR=y` |
| `mlog.h` | `dc309/include` | `dc100/include/mlog.h` 已存在 | ✅ |
| `hal_wifi.h`（HAL_WIFI_APMODE_CFG_S、HAL_WIFI_INTERFACE_NAME、HAL_WIFI_IP） | comps_install/hal | 同路径 | ✅ 两边 `hal_wifi.h` 完全相同（`HAL_WIFI_IP = 192.168.169.1`） |
| `media_init.h`（MEDIA_GetCtx、`venchdl[MAX_CAMERA_INSTANCES][MAX_VENC_CNT]`） | `dashcam/media/include` | 同路径 | ✅ |
| `cvi_comm_venc.h`（H264E_NALU_* 等） | comps_install/cvi_mpi | 同路径 | ✅ |
| `mapi_venc.h`（MAPI_VCODEC_E、MAPI_VENC_HANDLE_T） | comps_install/mapi | 同路径 | ✅ 枚举一致 |
| 子码流取流线程已运行 | `MEDIA_StartVideoInTask()` 在 `WORK_MODE_MOVIE` 进入时启动 | 同（`moviemode.c:252/381`） | ✅ 两边都由录像模式启动 `VIDEO_SERVICR_TaskStart`，`VIDEO_SERVICR_CallbackSet` 才有帧可拿 |
| SD 卡目录布局 | `/mnt/sd/DCIM/PHOTO`、`/mnt/sd/DCIM/MOVIE`、`/mnt/sd/DCIM/.thumb/PHOTO_SMALL|MOVIE_SMALL` | 同（`dc100/include/config.h` 的 `PHOTO_ALBUM_*_PATH` 一致） | ✅ |
| DTCF 重扫机制 | FILEMNG worker 500ms 轮询，`NOT_AVAILABLE→AVAILABLE` 触发 `DTCF_Scan()` | `applications/manager/src/file/filemng.c` 逻辑相同（仅空白符差异 + dc309 多一行 SetHiddenAttribute） | ✅ `jl25_storage_guard.c` 的状态翻转手法在 DC100 上同样有效 |
| 工具链/ABI | arm-none-linux-musleabihf | 同 | ✅ |

> **一个必须知道的差异**：DC100 的 `MEDIA_Size2VideoMediaMode/Size2PhotoMediaMode`（`media_init.h:171-172`）带 `desc` 参数，DC309 不带——但协议代码没有调用这两个函数，不受影响。DC100 的 `filemng.h` 多一个 `FILEMNG_DiscardFile`，也不冲突。

---

## 三、第二步：Makefile 接入

`applications/dashcam/Makefile`，两处改动：

1. **变量区**（`UI_DIR := ...` 那一行之后，dc309 Makefile 第 17 行的位置）：

```make
JL25_PROTOCOL_DIR		:= $(UI_DIR)/src/protocol
```

2. **INCS 区**（`INCS += -I$(UI_DIR)/src/guiguider_ui` 之后加一行）：

```make
INCS			+= -I$(JL25_PROTOCOL_DIR)
```

3. **SUBDIRS 区**（dc100 的 Makefile 里 `SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/guiguider_ui` 附近，注意 dc100 是通过 `UI_SRC_SUBDIRS` 展开的，直接在 `SUBDIRS += $(CURDIR)/mode/src` 前后加）：

```make
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/protocol
```

> `SRCS_TEMP := $(shell find $(SUBDIRS) -type f -name "*.c")` 是递归收集的（dc100/dc309 相同写法，见 dc100 Makefile:250），所以 `preview_rtsp/` 子目录下的 `.c` 会被自动收进来，不需要单独列。`preview_rtsp` 的头文件以 `#include "preview_rtsp/jl25_preview_rtsp.h"` 引用，相对 `-I$(JL25_PROTOCOL_DIR)` 就能找到，无需再加子目录 INCS。

不需要动 `Kconfig`/`defconfig`：协议代码用 `#ifdef WIFI_ON` 保护热点相关部分（`WIFI_ON` 由 `components/Kbuild` 在 `CONFIG_WIFI=y` 时注入 `KBUILD_DEFINES`，DC100 defconfig 已有 `CONFIG_WIFI=y` + `CONFIG_WIFI_RTL8189=y`，链路一致）。

---

## 四、第三步：热点 UI 页面（不能原样拷，按下面改）

DC309 的 `page_sysmenu_hotspot.c/.h` 可以作为底稿拷到 DC100 的 `guiguider_ui/`，然后做以下适配：

### 1. 替换 STA 状态检查

dc309 里这段（`hotspot_set_enabled` 开头，用于关热点前停掉 STA 连接）：

```c
if (wifi_manager_get_status() == 1) {
    wifi_manager_set_enabled(0);
}
```

DC100 没有 `wifi_manager.*`（那是 dc309 基于 `/tmp/aicam_wifi.sock` 的客户端）。DC100 对应能力在 `include/hal_wifi_ctrl.h`：

```c
#include "hal_wifi_ctrl.h"
// 判断 wlan0 是否已连接：
//   Hal_Wifi_Is_Up()          → 接口 up 状态（1=up）
//   Hal_Wpa_GetConnectSignal() != -1 → 当前有 STA 连接
```

建议改成：开热点前调用 `Hal_Wpa_DisableAllNetworks()`（或 `Hal_Wpa_Down()`），避免 wpa_supplicant 继续尝试连接路由器；关热点后调 `Hal_Wpa_EnableAllSavedNetworks()` 恢复。

### 2. WIFI_AP_Start/Stop 的 extern 声明保留

dc309 热点页顶部的写法在 dc100 原样可用（`hal_wifi_common.h` 不在 UI include 路径，两边都靠 extern）：

```c
int32_t WIFI_AP_Start(char* ifname, const HAL_WIFI_APMODE_CFG_S* pstApCfg);
int32_t WIFI_AP_Stop(void);
extern int32_t JL25_Start(void);
extern int32_t JL25_Stop(void);
```

`WIFI_AP_Start/WIFI_AP_Stop` 这两个符号在 **DC100 的 `libcomp_hal.so` 里已存在**（`nm -D` 确认 `T WIFI_AP_Start`/`T WIFI_AP_Stop`），链接没问题；问题在于它的**内部行为**（见第五节）。

### 3. 页面挂载

- `page_all.h` 加 `#include "page_sysmenu_hotspot.h"`；
- `page_sysmenu_setting.h` 的枚举加 `SYSMENU_HOTSPOT`；
- `page_sysmenu_setting.c` 菜单项数组与 case 分支仿照 dc309 的写法加入口（两边的 `ui_load_scr_animation(&g_ui, ...)`、`sysMenu_Xxx(lv_ui_t* ui)` 页面签名完全一致，dc309 的页面代码结构可以直接套）。

### 4. UI 线程注意点（照搬 dc309 的做法）

- `WIFI_AP_Start/Stop` 内部有 1~2 秒 usleep，必须 `pthread` 后台执行 + `lv_async_call` 回主线程刷新提示，否则 LVGL 卡死；
- 退出页面统一走 `hotspot_exit_page()`：先关热点（JL25_Stop → WIFI_AP_Stop）再销毁页面对象；
- 生命周期固定为：开热点成功 → `JL25_Start()`；关热点 → 先 `JL25_Stop()` 再 `WIFI_AP_Stop()`。

---

## 五、第四步：HAL 热点坑（DC100 的关键差异，必须处理）

DC309 的 `components/hal/src/wifi/common/hal_wifi_ap.c` + `rtl8189fs/wifi_hal.c` 里修了三个坑，**DC100 的 HAL 是预编译库没有源码**，且我反汇编/strings 对比确认 DC100 现有 `libcomp_hal.so`（comps_install dc100 分支）仍是老行为：

| | DC309 已修复 | DC100 现库实际状态 |
|---|---|---|
| hostapd.conf 生成 | `wpa=2` + `rsn_pairwise=CCMP` + `ieee80211n=0`（`wpa=3` 会报 `key not allowed`；HT 开着 beacon 不稳） | `wpa=3`、`wpa_pairwise=CCMP`（无 `rsn_pairwise`）、`ieee80211n=1` —— **坑还在** |
| 驱动干净切 AP | `WIFI_AP_Start` 前 `rmmod 8189fs` 再 `insmod`（否则 AP-ENABLED 但手机搜不到） | strings 中无 rmmod/insmod 8189fs 序列 —— **没有这个处理** |
| 杀 STA 干扰进程 | `killall wpa_supplicant wifi-daemon dhcpcd`；Stop 时恢复 | 只有 `/usr/bin/killall`、`udhcpd` 字符串，没有杀 wpa_supplicant 的逻辑 —— **没有这个处理** |
| WIFI_AP_Start/Stop 符号 | 有 | 有（链接 OK） |

三条路可选（按推荐顺序）：

### 方案 A（推荐）：把 DC309 的 HAL wifi 源码移植回 DC100 的 HAL 组件仓库

1. 找 comps_install 仓库的管理员（`git@github.com:sophcamd/comps_install.git`，分支 `dc100`）重新出库：把 DC309 `components/hal/src/wifi/` 下 `common/hal_wifi_ap.c`、`common/hal_wifi.c`、`rtl8189fs/wifi_hal.c`、`common/hal_wifi_common.h` 的修复同步进去，按 DC100 components/Makefile 的 cmake 参数（`-DCONFIG_WIFI=y -DCONFIG_WIFI_RTL8189=y ...`，见 `components/Makefile` tail 的 hal 分支）重新编译出 `libcomp_hal.so/.a` 推到 comps_install；
2. 本地临时验证可以不等仓库：直接在 `sophcam/components/` 下仿照 DC309 建 `hal/src/wifi/` 源码目录 + CMakeLists（DC309 的 `components/hal/CMakeLists.txt` 可参考，`CONFIG_WIFI_RTL8189` 开关已留好），make 时让本地源码版 `libcomp_hal` 覆盖 comps_install 的预编译版（注意 `components/Makefile` 里 `if [ -d comps_install/.git ]` 会跳过子组件编译，本地验证时临时绕开或单独 `make -C components hal`）。

### 方案 B：不动 HAL，把"坑处理"提到 UI 层

在 `page_sysmenu_hotspot.c` 的 `hotspot_set_enabled()` 里、调用 `WIFI_AP_Start()` 之前自己做：

```c
/* 1. 杀掉 STA 干扰进程 */
system("killall wpa_supplicant dhcpcd 2>/dev/null");
/* 2. 驱动从干净 AP 状态启动 */
system("rmmod 8189fs; insmod /system/ko/3rd/8189fs.ko");
usleep(...)  /* 等驱动就绪 */
/* 3. 再调 WIFI_AP_Start(...) */
```

关热点时反向恢复（`wpa_supplicant -D nl80211 -i wlan0 -c /etc/wpa_supplicant.conf -B &`，参考 `S99user` 的启动命令）。

**hostapd 配置问题方案 B 覆盖不了**：`WIFI_HAL_UpdateApConfig` 在库里是运行时生成 `/dev/wifi/hostapd/hostapd.conf` 的，UI 层可以在 `WIFI_AP_Start()` 返回后、起 hostapd 前抢不到时机；退而求其次的做法是 UI 层自己写完整 hostapd 流程（hostapd.conf 用 `wpa=2 + rsn_pairwise=CCMP + ieee80211n=0`，可参考 rootfs 现成的 `/etc/network/hostapd.conf` 模板改），彻底绕开 `WIFI_AP_Start` 内部的配置生成。这等于在 UI 层重写一份 `WIFI_AP_Start`，工作量与方案 A 差不多，但都堆在页面代码里，长期不好维护。

### 无论哪个方案都要做的：udhcpd.conf

DC309 的热点 IP 是 `192.168.169.1`（`HAL_WIFI_IP`，DC100 头文件同值），DHCP 地址池必须匹配，否则手机连上拿不到 IP。DC100 当前 rootfs 的 `/etc/network/udhcpd.conf` 还是 `192.168.1.x` 网段：

改 `install/soc_cv1842cp_dc100_spinand/rootfs/etc/network/udhcpd.conf`（同时建议改 `ramdisk/rootfs/overlay` 的源头，DC309 这边就吃过"重打包回退"的亏，源头 `ramdisk/rootfs/overlay/cv184x_glibc_arm64/etc/network/udhcpd.conf` 至今还是 `192.168.1.x`）：

```text
start       192.168.169.10
end         192.168.169.254
opt router  192.168.169.1
option subnet 255.255.255.0
interface   wlan0
```

改完重新打包 rootfs 烧录（或先直接替换板端 `/etc/network/udhcpd.conf` 验证）。

热点 SSID/密码从参数层读取：DC100 的 `config_devmng.ini` 同样有 `[wifi_config]`（`ssid = "cvi_cardv_ap"`、`password = "12345678"`），dc309 热点页读 `PARAM_GetWifiParam()` 的代码不用改。为支持 APP 主动连接，`/app/getdeviceattr` 的 `info.ssid` 从 AP 配置 `WifiCfg.unCfg.stApCfg.stCfg.szWiFiSSID` 返回该热点名称，供 APP 与手机当前连接的 WiFi 名称比较；密码和旧版 WiFi 配置接口仍不对 APP 暴露。

---

### 5. 热点状态与 APP 连接提示

DC100 的 `PARAM_WIFI_S.Enable` 表示普通 WiFi/STA 状态，不表示 AP 热点状态。热点页面的
`hs_switch`、状态圆点和状态文字必须使用 `JL25_IsRunning()` 判断，只有热点对应的 JL25
协议服务运行时才显示热点开启。这样即使设备开机时普通 WiFi 默认开启，也不会把热点状态
误判为开启。

APP 成功建立 TCP `6035` 长连接并完成客户端线程创建后，`jl25_appsock.c` 调用连接回调；
热点页面通过已有多语言词条 `str_language_connection_successful` 和
`top_notice_show_for()` 显示“连接成功”。`top_notice_show_for()` 内部使用
`lv_async_call()`，可安全地从协议线程触发 UI 提示。

DC309 后续修改时需同步以上两处逻辑：不要用 `WifiParam.Enable` 作为热点开关状态，
并在 `jl25_appsock.c` 的 TCP `6035` 连接成功路径触发 UI 提示回调。

---

## 六、第五步：编译与部署

```bash
cd /home/mayi/SDK_CV184X_DC100/sophcam

# HAL 若走了方案 A 且需要重编组件：
make cv1842cp_dc100_defconfig
make -C components        # 重新生成 libcomp_hal
make -j$(nproc) && make install

# 只改了 UI/协议层时增量编译：
make -C applications
make pack_data            # 打包 data 分区镜像（含 udhcpd.conf 的 rootfs 需走 BSP 打包）
```

部署到板子（`/mnt/data/bin/sophcam`）后按 DC309 交接文档的顺序验证：

1. `netstat -tln | grep -E ':80 |:8222|:554|:6035'` 确认四个端口监听；
2. 手机连热点（`cvi_cardv_ap` / `12345678`），浏览器 `http://192.168.169.1/Misc?GetCurrentWorkMode` 应返回 `{"Result":0,"Mode":"Camera"}`；
3. `nc 192.168.169.1 8222` 挂着收事件，插拔 SD 卡验证 `{"Event":12/13}`；
4. APP 侧：识别 → 预览（`rtsp://192.168.169.1:554/liveRTSP/av4`）→ 拍照 → 录像 → 相册/下载，完整链路回归；
5. 观察 `syslog/messages` 中的 `jl25` 前缀日志（协议服务启动会打印 `settings_policy=sd-format-only ... policy_version=5`，APP 连接后打印 `APP connected, active file scan start/completed`）。

---

## 七、移植点核对清单（快速过一遍）

| # | 事项 | 类型 | 位置 |
|---|---|---|---|
| 1 | 整体拷 `src/protocol/`（20 文件） | 直接拷 | `dc100/src/protocol/` |
| 2 | Makefile 加 `JL25_PROTOCOL_DIR` + INCS + SUBDIRS | 必改 | `applications/dashcam/Makefile` |
| 3 | 热点页 `page_sysmenu_hotspot.c/h` 拷入并替换 STA 控制为 `hal_wifi_ctrl` 接口 | 适配 | `dc100/src/guiguider_ui/` |
| 4 | `page_all.h` / `page_sysmenu_setting.c/h` 加热点入口 | 必改 | `dc100/src/guiguider_ui/` |
| 5 | HAL 坑修复：`wpa=2/rsn_pairwise/ieee80211n=0`、rmmod/insmod、killall | **关键差异** | comps_install `libcomp_hal`（方案 A）或 UI 层补（方案 B） |
| 6 | `udhcpd.conf` 改 169 网段（install + overlay 源头都改） | 必改 | rootfs |
| 7 | mlog.h / include 路径 | 已存在，无需动 | `dc100/include/` |
| 8 | defconfig | 无需动（WIFI/RTSP/SUBVIDEO/THUMBNAIL 全已开） | `configs/cv1842cp_dc100_defconfig` |

## 八、DC100 上可以预期与 DC309 一致的行为

- 工作模式自动切换：APP 请求预览/录像/拍照时协议层自动切 `WORK_MODE_MOVIE`/`WORK_MODE_PHOTO` 并等待完成，无需本地手动切页面；
- 拍照走照片模式路径（两块板子都没开 `CONFIG_ENABLE_SNAP`），停 RTSP → 切 PHOTO → `EVENT_MODEMNG_START_PIV` → 等文件落盘 → 切回 MOVIE → 恢复 RTSP；
- 缩略图 JPEG 完整性校验（FF D8/FF D9）、512KB 限制解除、原图回退逻辑随 `jl25_storage.c` 直接带过来；
- WiFi 名称/密码隐藏策略（`wifi_probe=http-404/absent`、日志脱敏、`policy_version=5`）随 `jl25_legacy.c`/`jl25_app.c` 直接带过来；
- `/app/getdeviceattr` 返回 AP 热点 SSID，供 APP 与手机当前 WiFi 名称比较；普通 WiFi/STA 开关与 AP 热点状态分离；
- TCP `6035` 长连接建立成功后，热点页面显示“连接成功”提示；
- APP 主动扫描（6035 建链后 FILEMNG 状态翻转触发 DTCF 重扫）依赖的 FILEMNG worker 行为两边一致，`jl25_storage_guard.c` 无需改动。

## 九、风险与待验证项

1. **HAL 库（最大风险点）**：方案 A/B 二选一后，务必按 DC309 的三个坑逐条验证：`AP-STA-CONNECTED` 出现（搜得到）、`hostapd` 日志无 `key not allowed`、开关热点后 dmesg 无 `rtw_set_802_11_connect` 反复刷；
2. DC100 是 640x480 单屏 UI，热点页的布局尺寸（dc309 页面用的字体/控件大小）需要按 `CONFIG_UI_WIDTH=640 / CONFIG_UI_HEIGHT=480` 微调；
3. DC100 侧 `moviemode.c` 有 PiP（画中画）相关逻辑（`MEDIA_IsPiPEnabled`），若产品开了画中画，切 `WORK_MODE_MOVIE` 时 RTSP 子码流的绑定行为需板端实测一遍（dc309 无此逻辑）；
4. `wifi-daemon` 在 DC100 的 S99user 里有启动行但二进制不存在——移植后若走了方案 B（UI 层 killall），注意 `killall wifi-daemon` 无害可以直接保留；若后续 DC100 出了 wifi-daemon，则热点页的 STA 关闭逻辑要再切回 socket 方式；
5. 联调工具 `docs/jl25_curl_test.sh` 可直接拷到 DC100 的 docs 下复用（28 项回归）。

## 十、DC100 后续必须同步的协议修复

以下内容是 DC100 项目后续协议层改动项；本次只更新移植文档，未修改
`SDK_CV184X_DC100` 源码。

### 1. 6.1.3：媒体信息能力标识

DC100 的 `/app/getmediainfo` 应在 `info` 中返回 `rectime:1`：

```json
{"result":0,"info":{"rtsp":"rtsp://192.168.169.1:554/liveRTSP/av4",
"transport":"udp","port":6035,"page":1,"rectime":1}}
```

该字段通知 APP 使用 `/app/getrecduration` 轮询当前录像时长。

### 2. 6.1.4：SD 卡返回状态

DC100 应复用 DC309 协议层的状态映射，不直接把 `MODEMNG_GetCardState()` 原值透传给 APP：

| `status` | 含义 | DC100 映射 |
|---:|---|---|
| 0 | 正常 | `CARD_STATE_AVAILABLE` / `CARD_STATE_FORMATED` |
| 1 | 未格式化或不可用 | `CARD_STATE_UNAVAILABLE` |
| 2 | 未插入 | `CARD_STATE_REMOVE` |
| 3 | 卡损坏 | `CARD_STATE_ERROR` |
| 4 | 卡已满 | `CARD_STATE_FULL_SPACE` |
| 10 | 写保护 | `CARD_STATE_READ_ONLY` |
| 11 | 卡速过慢 | `CARD_STATE_SLOW` |
| 12 | 文件系统/挂载异常 | `CARD_STATE_FSERROR` / `CARD_STATE_MOUNT_FAILED` |
| 13 | 正在格式化 | 协议层格式化任务标志 |
| 99 | 检测中或未知 | `CARD_STATE_CHECKING` 及未覆盖状态 |

`/app/getsdinfo` 正常时返回 `free`、`total`（MiB），异常时至少返回 `status`。
事件推送保持插卡 `Event:12`、拔卡 `Event:13`、错误 `Event:10`、卡满 `Event:11`、写保护 `Event:16`。

### 3. 6.1.8：实际录像时长

DC100 当前基线代码中的 `/app/getrecduration` 固定返回 0，需要改为动态实现：

1. `jl25_capture_start_record()` 等待设备进入 `MEDIA_MOVIE_STATE_REC` 或
   `MEDIA_MOVIE_STATE_LAPSE_REC` 后记录 `CLOCK_MONOTONIC` 起点。
2. `jl25_capture_stop_record()` 等待退出录像状态后清除起点。
3. `/app/getrecduration` 录像中计算单调时钟差，按整数秒返回；非录像状态返回 0。
4. 读取 `PARAM_MEDIA_COMM_S.Record.ChnAttrs[0].SplitTime`（毫秒），循环录像切片时取模，
   返回当前录像文件时长而不是整个录像会话时长。
5. `/app/getmediainfo` 必须同时返回 `rectime:1`，否则 APP 可能不发起时长轮询。

标准响应：

```json
{"result":0,"info":{"duration":56}}
```

建议只改 DC100 `src/protocol/`，在 `jl25_internal.h` 增加计时函数声明，
并在 `jl25_app.c`、`jl25_setup.c` 中按 DC309 的实现同步；不要改媒体层、模式层或 UI 层。

### 4. DC100 协议验收清单

```bash
curl -s http://192.168.169.1/app/getmediainfo
curl -s http://192.168.169.1/app/getsdinfo
curl -s http://192.168.169.1/app/getrecduration
curl -s 'http://192.168.169.1/app/setparamvalue?param=rec&value=1'
sleep 3
curl -s http://192.168.169.1/app/getrecduration
curl -s 'http://192.168.169.1/app/setparamvalue?param=rec&value=0'
```

预期：媒体信息含 `rectime:1`；录像过程中 `duration` 增长；停止后回到 0；
SD 卡各异常状态按上表返回。DC100 后续编译时需关注计时状态声明必须位于
`jl25_app_get_recduration()` 之前，避免 C 编译器出现 `undeclared` 错误。

### 5. 远程录像断开与音频 DTS（DC100 已确认存在同类风险）

核对日期：2026-09-11。本节仅记录 DC100 基线代码的核对结论，**未修改**
`SDK_CV184X_DC100` 中任何源码。

DC100 当前的 `jl25_capture_start_record()` 与修复前 DC309 的实现一致：直接发送
`EVENT_MODEMNG_START_REC`，没有在开始新录像前重建 recorder，也没有记录“该录像由
APP 发起”的状态。TCP `6035` 客户端线程断开时仅关闭 fd 并递减客户端计数；`JL25_Stop()`
也只停止协议服务。因此当前存在以下确认问题：

```text
APP 开始远程录像 -> APP 断开/设备关闭热点 -> 设备仍继续录像
```

连续 APP 录像还存在与 DC309 相同的跨文件时间轴风险：上一会话残留帧或 audio DTS 未清理时，
可能出现 `audio dts <= last ... adjust` 日志，进而导致 APP 回放跳秒或本地回放无声。该项需在
板端连续录像后结合日志确认。

后续只需在 DC100 的 `src/protocol/` 内同步处理，建议范围如下：

| 文件 | 后续协议层改动 |
|---|---|
| `jl25_setup.c` | 增加远程录像标记；开始前执行 `MEDIA_RecordSerDeInit()` / `MEDIA_RecordSerInit()`；以互斥保护开始/停止；显式停止时清除标记。 |
| `jl25_appsock.c` | 增加 TCP `6035` 客户端断开回调；仅最后一个 APP 客户端断开时通知协议层。 |
| `jl25_http.c` | 注册断开回调，并在 `JL25_Stop()` 中触发远程录像清理。 |
| `jl25_protocol.h` | 声明断开回调类型及注册接口。 |
| `jl25_internal.h` | 声明协议停止和 APP 断开时的内部清理接口。 |

行为边界：只停止由 APP 成功开启的录像，不能误停本地 UI 录像；多客户端时仅最后一个断开才
停止；退出热点页或关闭热点必须停止远程录像；录像前重建 recorder 不能停止 RTSP 预览或切换
工作模式；若重建失败，本次开始录像必须失败，不能继续下发录像开始事件。

验收建议：

1. 连续两次 APP 录像各 7～10 秒，日志没有 `audio dts <= last ... adjust`，两个文件均有声音。
2. APP 录像期间关闭 APP 或断开手机 WiFi，设备停止远程录像。
3. APP 录像期间退出热点页或关闭热点，设备停止远程录像。
4. 本地 UI 启动录像后让 APP 断开或关闭热点，本地录像不被停止。
