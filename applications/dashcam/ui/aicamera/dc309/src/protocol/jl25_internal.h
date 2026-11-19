/* JL25xx 协议模块共享定义（协议实现与热点 UI 共用） */
#ifndef __JL25_INTERNAL_H__
#define __JL25_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>

/* 命令分发（jl25_cmd.c）：按 path 路由到 JL25xx 和旧 CGI 兼容入口
 * path : "/Misc" "/SetMode" "/Storage" "/Setup" "/Pin"
 * query: "?" 后面的原始参数串，如 "GetFileTHumb=fid&width=160"
 */
void JL25_HandleRequest(const char* path, const char* query,
    const char* request, int fd);
void jl25_legacy_handle(const char* path, const char* query, int fd);
void jl25_app_handle(const char* path, const char* query,
    const char* request, int fd);
int jl25_app_is_media_path(const char* path);
void jl25_app_handle_media_path(const char* path, const char* request, int fd);
int jl25_preview_ensure_movie_mode(void);
int jl25_preview_start(void);
int jl25_preview_stop(void);
int jl25_preview_is_active(void);
void jl25_preview_build_rtsp_url(char* buf, size_t buf_sz);
/* Build the runtime AP SSID shared by the hotspot UI and APP protocol. */
void jl25_make_ap_ssid(const char* base, char* out, size_t out_sz);
int jl25_capture_start_record(void);
int jl25_capture_stop_record(void);
void jl25_capture_app_disconnected(void);
void jl25_capture_protocol_stopping(void);
int jl25_capture_take_photo(void);
int jl25_capture_is_recording(void);
void jl25_app_record_duration_start(void);
void jl25_app_record_duration_stop(void);
int jl25_appsock_start(void);
void jl25_appsock_stop(void);
int jl25_appsock_has_client(void);
int jl25_discovery_start(void);
void jl25_discovery_stop(void);
void jl25_discovery_note_peer(int fd);

/* HTTP 响应发送工具（jl25_http.c 实现） */
int jl25_http_send_response(int fd, int code, const char* code_text,
    const char* content_type, const char* body,
    size_t body_len);
int jl25_http_send_all(int fd, const void* buf, size_t len);
int jl25_http_send_json(int fd, const char* json);
int jl25_http_send_binary(int fd, const char* content_type,
    const char* body, size_t body_len);

/* query 工具（jl25_cmd.c 实现）
 * jl25_query_get: 取 query 中 param= 的值写入 val，找到返回 val，找不到返回 NULL
 */
const char* jl25_query_get(const char* query, const char* param,
    char* val, size_t val_sz);

/* 各命令组入口（各 jl25_xxx.c 实现） */
void jl25_misc_handle(const char* query, int fd);
void jl25_setmode_handle(const char* query, int fd);
void jl25_storage_handle(const char* query, int fd);
void jl25_storage_send_thumb_fid(const char* fid, int fd);
void jl25_storage_download_fid(const char* fid, uint64_t offset,
    const char* request, int fd);
int jl25_storage_delete_fid(const char* fid);
int jl25_storage_access_begin(void);
void jl25_storage_access_end(void);
int jl25_storage_format_card(void);
int jl25_storage_get_app_status(void);
void jl25_storage_format_event(uint32_t topic);
void jl25_storage_request_scan(void);
int jl25_factory_reset(void);
void jl25_setup_handle(const char* query, int fd);

#endif /* __JL25_INTERNAL_H__ */
