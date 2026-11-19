/*
 * JL25xx WiFi CGI 协议管理模块
 *
 * 实现《JL25xx_Wifi_CGI_Control_Interface》协议（docs/JL25xx_Wifi_CGI_Control_Interface.pdf）：
 *   - HTTP:80   命令端口：/Misc? /SetMode? /Storage? /Setup? /Pin?
 *   - TCP:8222  事件输出端口：{"Event":N} JSON 推送
 *   - TCP:6035  旧版 APP 长连接
 *   - UDP:49142 旧版 APP 自动建链状态通知
 *
 * 模块生命周期由热点页面控制：开热点时 JL25_Start()，关热点时 JL25_Stop()。
 * 不依赖 thttpd/netctrl（dc309 未启用 CONFIG_APPLICATION_NET，且 thttpd 的
 * URL 模式匹配写死，无法路由 /Misc? 等 JL25xx URL）。
 */
#ifndef __JL25_PROTOCOL_H__
#define __JL25_PROTOCOL_H__

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 配置常量 ==================== */
#define JL25_HTTP_PORT 80 /* 协议命令端口 */
#define JL25_EVENT_PORT 8222 /* 事件输出端口 */
#define JL25_MAX_CLIENTS 4 /* 最大并发连接 */
#define JL25_REQ_HEADER_MAX 2048 /* 请求头缓冲上限 */
#define JL25_RESP_BUF_INIT 4096 /* 响应缓冲初始大小 */
#define JL25_FILE_CHUNK_SIZE (256 * 1024) /* 文件下载块大小 */
#define JL25_THUMB_MAX_SIZE (512 * 1024) /* 兼容旧配置，响应不再受此值限制 */

/* 协议版本（APP 通过 /Misc?Info 获取，当前为 1） */
#define JL25_PROTOCOL_VER 1

/* 事件推送编号（协议 4.3.1 节） */
typedef enum {
    JL25_EVENT_REC_STOPPED = 1, /* MP4 录影结束（秒数到/空间满） */
    JL25_EVENT_IR_ALLPASS = 2, /* IR 切为 AllPass / 补光灯开 */
    JL25_EVENT_IR_CUT = 3, /* IR 切为 IR-Cut / 补光灯关 */
    JL25_EVENT_STREAM_ERROR = 4, /* 8221 流错误，APP 需重连 */
    JL25_EVENT_DOWNLOAD_ABORT = 5, /* 文件传输被取消 */
    JL25_EVENT_LOW_BATTERY = 6, /* 没电，即将关 WiFi AP */
    JL25_EVENT_FORCE_POWEROFF = 7, /* 设备强制关机 */
    JL25_EVENT_SD_ERROR = 10, /* SD 卡错误 */
    JL25_EVENT_SD_FULL = 11, /* SD 卡满 */
    JL25_EVENT_SD_INSERT = 12, /* SD 卡插入 */
    JL25_EVENT_SD_REMOVE = 13, /* SD 卡拔出 */
    JL25_EVENT_SD_READFAIL = 15, /* SD 卡读失败 */
    JL25_EVENT_SD_WRITEPROTECT = 16, /* SD 卡写保护 */
    JL25_EVENT_DOWNLOAD_1MB = 17, /* 下载每 1MB 上报（附电量） */
} JL25_EVENT_E;

/* 工作模式（/Misc?GetCurrentWorkMode 返回的字符串） */
typedef enum {
    JL25_WORKMODE_CAMERA = 0,
    JL25_WORKMODE_STORAGE,
    JL25_WORKMODE_PHOTOCAPTURE,
    JL25_WORKMODE_VIDEOCAPTURE,
    JL25_WORKMODE_SETUP,
} JL25_WORKMODE_E;

/* 文件类型（GetFileList/GetFilePage 中 "t" 字段） */
typedef enum {
    JL25_FILETYPE_JPG = 1,
    JL25_FILETYPE_AVI = 2,
    JL25_FILETYPE_TLAVI = 3,
    JL25_FILETYPE_WAV = 4,
    JL25_FILETYPE_MP4 = 5,
} JL25_FILETYPE_E;

/* ==================== 公共 API ==================== */

/* 启动协议服务（HTTP:80、TCP:8222、TCP:6035、UDP:49142）。线程安全 */
int32_t JL25_Start(void);

/* 停止协议服务并释放资源。线程安全，未启动时调用直接返回 0 */
int32_t JL25_Stop(void);

/* 查询服务是否在运行 */
int32_t JL25_IsRunning(void);

/* 注册 APP 连接成功回调（TCP:6035 建立后触发，传 NULL 取消） */
typedef void (*JL25_APP_CONNECTION_CB)(void);
void JL25_SetAppConnectionCallback(JL25_APP_CONNECTION_CB callback);

/* 注册 APP 长连接断开回调（TCP:6035 客户端断开后触发） */
typedef void (*JL25_APP_DISCONNECTION_CB)(void);
void JL25_SetAppDisconnectionCallback(JL25_APP_DISCONNECTION_CB callback);

/* 推送事件给所有已连接的 APP（内部加锁，任意线程可调用）
 * ev     : JL25_EVENT_E
 * extra  : 附加 JSON 字段，如 "\"BatteryLevel\":3"，可为 NULL
 */
int32_t JL25_PushEvent(int32_t ev, const char* extra);

/* ==================== 内部共享工具（协议子模块用） ==================== */

/* 工作模式枚举 -> 协议字符串 */
const char* JL25_WorkModeToStr(int mode);

/* 协议字符串 -> 工作模式枚举，未识别返回 -1 */
int JL25_StrToWorkMode(const char* str);

#ifdef __cplusplus
}
#endif

#endif /* __JL25_PROTOCOL_H__ */
