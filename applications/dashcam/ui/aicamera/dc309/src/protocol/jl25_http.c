/*
 * JL25xx 协议 HTTP 命令服务器 + TCP 事件推送
 *
 * 架构：
 *   - HTTP:80：accept 线程为每个连接创建独立处理线程（短连接为主，支持 Keep-Alive）
 *   - TCP:8222：accept 后将客户端 fd 加入事件广播列表，锁保护
 *
 * 命令分发：jl25_cmd.c 提供 JL25_HandleRequest(path, query, conn)
 */
#define DEBUG
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jl25_events.h"
#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "mlog.h"

/* ==================== 状态 ==================== */
static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_running = 0;

static int s_http_listen_fd = -1;
static int s_event_listen_fd = -1;
static pthread_t s_http_accept_thread;
static pthread_t s_event_accept_thread;

/* 事件客户端列表 */
#define JL25_EVENT_CLIENT_MAX 8
static int s_event_clients[JL25_EVENT_CLIENT_MAX];
static int s_event_client_cnt = 0;
static pthread_mutex_t s_event_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 工具 ==================== */

static void jl25_set_keepalive(int fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
}

static void jl25_set_reuseaddr(int fd)
{
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

/* ==================== 事件推送 ==================== */

int32_t JL25_PushEvent(int32_t ev, const char* extra)
{
    char buf[256];
    int len;

    if (extra != NULL && extra[0] != '\0') {
        len = snprintf(buf, sizeof(buf), "{\"Event\":%d,%s}\r\n", ev, extra);
    } else {
        len = snprintf(buf, sizeof(buf), "{\"Event\":%d}\r\n", ev);
    }

    pthread_mutex_lock(&s_event_mutex);
    for (int i = 0; i < JL25_EVENT_CLIENT_MAX; i++) {
        if (s_event_clients[i] < 0) {
            continue;
        }
        ssize_t n = send(s_event_clients[i], buf, len, MSG_NOSIGNAL);
        if (n < 0) {
            /* 发送失败则踢掉该客户端 */
            MLOG_DBG("jl25: event client %d dead, drop\n", i);
            close(s_event_clients[i]);
            s_event_clients[i] = -1;
            s_event_client_cnt--;
        }
    }
    pthread_mutex_unlock(&s_event_mutex);

    return 0;
}

/* ==================== HTTP 处理 ==================== */

/* 发送完整缓冲区，send() 可能只接受部分数据。 */
int jl25_http_send_all(int fd, const void* buf, size_t len)
{
    const char* p = (const char*)buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

/* 发送完整 HTTP 响应（带 Content-Length 头） */
int jl25_http_send_response(int fd, int code, const char* code_text,
    const char* content_type, const char* body,
    size_t body_len)
{
    char header[512];
    int header_len;

    if (content_type == NULL) {
        content_type = "text/html";
    }
    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: SOPHCAM-1.0\r\n"
        "Connection: close\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        code, code_text, content_type, body_len);

    /* 头 + 体一起发，减少小包 */
    size_t total = header_len + body_len;
    char* out = (char*)malloc(total);
    if (out == NULL) {
        return -1;
    }
    memcpy(out, header, header_len);
    if (body_len > 0 && body != NULL) {
        memcpy(out + header_len, body, body_len);
    }
    int ret = jl25_http_send_all(fd, out, total);
    free(out);
    return ret;
}

/* 发送 JSON 响应（协议标准响应格式） */
int jl25_http_send_json(int fd, const char* json)
{
    return jl25_http_send_response(fd, 200, "OK", "text/html",
        json, strlen(json));
}

/* 发送二进制响应（缩略图/文件下载） */
int jl25_http_send_binary(int fd, const char* content_type,
    const char* body, size_t body_len)
{
    return jl25_http_send_response(fd, 200, "OK", content_type,
        body, body_len);
}

/* 解析 "GET /xxx?yyy HTTP/1.1" 请求行
 * 返回 0 成功；path/query 输出参数（内部静态/调用方提供缓冲）
 */
static int jl25_parse_request_line(char* line, char* path, size_t path_sz,
    char* query, size_t query_sz)
{
    /* Only parse the request line; the input buffer also contains headers. */
    char* line_end = strstr(line, "\r\n");
    if (line_end == NULL) {
        line_end = strchr(line, '\n');
    }
    if (line_end != NULL) {
        *line_end = '\0';
    }

    /* GET /path?query HTTP/1.1 */
    char* sp = strchr(line, ' ');
    if (sp == NULL) {
        return -1;
    }
    char* uri = sp + 1;
    char* ep = strchr(uri, ' ');
    if (ep == NULL) {
        return -1;
    }
    *ep = '\0';

    char* qm = strchr(uri, '?');
    if (qm != NULL) {
        *qm = '\0';
        snprintf(path, path_sz, "%s", uri);
        snprintf(query, query_sz, "%s", qm + 1);
    } else {
        snprintf(path, path_sz, "%s", uri);
        query[0] = '\0';
    }
    return 0;
}

/* 读完整请求头（直到 \r\n\r\n），带超时 */
static int jl25_read_request(int fd, char* buf, size_t buf_sz)
{
    size_t total = 0;
    while (total < buf_sz - 1) {
        fd_set rfds;
        struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            break; /* 超时或错误 */
        }
        ssize_t n = recv(fd, buf + total, buf_sz - 1 - total, 0);
        if (n <= 0) {
            break;
        }
        total += n;
        buf[total] = '\0';
        /* 头结束标志 */
        if (strstr(buf, "\r\n\r\n") != NULL) {
            return 0;
        }
    }
    return (total > 0) ? 0 : -1;
}

/* HTTP 连接处理线程 */
static void* jl25_http_conn_thread(void* arg)
{
    int fd = (int)(intptr_t)arg;
    char* req = (char*)malloc(JL25_REQ_HEADER_MAX);
    char path[256];
    char query[512];
    char raw_req[JL25_REQ_HEADER_MAX];

    if (req == NULL) {
        close(fd);
        return NULL;
    }
    prctl(PR_SET_NAME, "jl25HttpConn", 0, 0, 0);

    while (s_running) {
        if (jl25_read_request(fd, req, JL25_REQ_HEADER_MAX) != 0) {
            break;
        }
        /* Request-line parsing terminates the first line in-place. */
        snprintf(raw_req, sizeof(raw_req), "%s", req);

        if (jl25_parse_request_line(req, path, sizeof(path),
                query, sizeof(query))
            != 0) {
            jl25_http_send_response(fd, 400, "Bad Request",
                "text/html", "", 0);
            break;
        }

        MLOG_INFO("jl25 req path=[%s] query=[%s]\n", path, query);

        /* 协议命令分发（jl25_cmd.c） */
        JL25_HandleRequest(path, query, raw_req, fd);

        /* 当前全部短连接（Connection: close），处理完即断开 */
        break;
    }

    free(req);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return NULL;
}

/* HTTP accept 线程 */
static void* jl25_http_accept_thread_fn(void* arg)
{
    (void)arg;
    prctl(PR_SET_NAME, "jl25Http", 0, 0, 0);

    while (s_running) {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int cfd = accept(s_http_listen_fd, (struct sockaddr*)&addr, &addrlen);
        if (cfd < 0) {
            if (s_running && errno != EINTR) {
                MLOG_ERR("jl25: http accept fail, errno=%d\n", errno);
            }
            break;
        }
        jl25_set_keepalive(cfd);

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&tid, &attr, jl25_http_conn_thread,
                (void*)(intptr_t)cfd)
            != 0) {
            close(cfd);
        }
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

/* 事件端口 accept 线程 */
static void* jl25_event_accept_thread_fn(void* arg)
{
    (void)arg;
    prctl(PR_SET_NAME, "jl25Event", 0, 0, 0);

    while (s_running) {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int cfd = accept(s_event_listen_fd, (struct sockaddr*)&addr, &addrlen);
        if (cfd < 0) {
            if (s_running && errno != EINTR) {
                MLOG_ERR("jl25: event accept fail, errno=%d\n", errno);
            }
            break;
        }

        pthread_mutex_lock(&s_event_mutex);
        int slot = -1;
        if (s_event_client_cnt < JL25_EVENT_CLIENT_MAX) {
            for (int i = 0; i < JL25_EVENT_CLIENT_MAX; i++) {
                if (s_event_clients[i] < 0) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot >= 0) {
            s_event_clients[slot] = cfd;
            s_event_client_cnt++;
            MLOG_DBG("jl25: event client connected, slot=%d\n", slot);
        } else {
            MLOG_DBG("jl25: event clients full, reject\n");
            close(cfd);
        }
        pthread_mutex_unlock(&s_event_mutex);
    }
    return NULL;
}

/* ==================== 启停 ==================== */

static int jl25_create_listen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        MLOG_ERR("jl25: socket fail, errno=%d\n", errno);
        return -1;
    }
    jl25_set_reuseaddr(fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        MLOG_ERR("jl25: bind port %d fail, errno=%d\n", port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 8) != 0) {
        MLOG_ERR("jl25: listen port %d fail\n", port);
        close(fd);
        return -1;
    }
    return fd;
}

int32_t JL25_Start(void)
{
    int32_t ret = 0;

    pthread_mutex_lock(&s_state_mutex);
    if (s_running) {
        pthread_mutex_unlock(&s_state_mutex);
        return 0;
    }

    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < JL25_EVENT_CLIENT_MAX; i++) {
        s_event_clients[i] = -1;
    }
    s_event_client_cnt = 0;

    s_http_listen_fd = jl25_create_listen(JL25_HTTP_PORT);
    if (s_http_listen_fd < 0) {
        ret = -1;
        goto EXIT;
    }
    s_event_listen_fd = jl25_create_listen(JL25_EVENT_PORT);
    if (s_event_listen_fd < 0) {
        close(s_http_listen_fd);
        s_http_listen_fd = -1;
        ret = -1;
        goto EXIT;
    }

    s_running = 1;

    if (pthread_create(&s_http_accept_thread, NULL,
            jl25_http_accept_thread_fn, NULL)
        != 0) {
        s_running = 0;
        close(s_http_listen_fd);
        close(s_event_listen_fd);
        s_http_listen_fd = -1;
        s_event_listen_fd = -1;
        ret = -1;
        goto EXIT;
    }
    if (pthread_create(&s_event_accept_thread, NULL,
            jl25_event_accept_thread_fn, NULL)
        != 0) {
        s_running = 0;
        pthread_cancel(s_http_accept_thread);
        pthread_join(s_http_accept_thread, NULL);
        close(s_http_listen_fd);
        close(s_event_listen_fd);
        s_http_listen_fd = -1;
        s_event_listen_fd = -1;
        ret = -1;
        goto EXIT;
    }

    MLOG_DBG("jl25: protocol server started (http:%d event:%d)\n",
        JL25_HTTP_PORT, JL25_EVENT_PORT);

    /* 订阅设备事件转发到 TCP:8222 */
    JL25_EventsInit();
    JL25_SetAppDisconnectionCallback(jl25_capture_app_disconnected);
    if (jl25_appsock_start() != 0) {
        MLOG_ERR("jl25 appsock start fail\n");
    }
    if (jl25_discovery_start() != 0) {
        MLOG_ERR("jl25 discovery start fail\n");
    }

EXIT:
    pthread_mutex_unlock(&s_state_mutex);
    return ret;
}

int32_t JL25_Stop(void)
{
    pthread_mutex_lock(&s_state_mutex);
    if (!s_running) {
        pthread_mutex_unlock(&s_state_mutex);
        return 0;
    }

    s_running = 0;

    JL25_EventsDeinit();
    jl25_discovery_stop();
    jl25_appsock_stop();

    /* 关监听 fd 使 accept 返回错误退出线程 */
    if (s_http_listen_fd >= 0) {
        shutdown(s_http_listen_fd, SHUT_RDWR);
        close(s_http_listen_fd);
        s_http_listen_fd = -1;
    }
    if (s_event_listen_fd >= 0) {
        shutdown(s_event_listen_fd, SHUT_RDWR);
        close(s_event_listen_fd);
        s_event_listen_fd = -1;
    }
    pthread_mutex_unlock(&s_state_mutex);

    pthread_join(s_http_accept_thread, NULL);
    pthread_join(s_event_accept_thread, NULL);

    /* 断开所有事件客户端 */
    pthread_mutex_lock(&s_event_mutex);
    for (int i = 0; i < JL25_EVENT_CLIENT_MAX; i++) {
        if (s_event_clients[i] >= 0) {
            shutdown(s_event_clients[i], SHUT_RDWR);
            close(s_event_clients[i]);
            s_event_clients[i] = -1;
        }
    }
    s_event_client_cnt = 0;
    pthread_mutex_unlock(&s_event_mutex);

    /* 关闭热点/协议服务时也必须结束由 APP 打开的远程录像。 */
    jl25_capture_protocol_stopping();

    MLOG_DBG("jl25: protocol server stopped\n");
    return 0;
}

int32_t JL25_IsRunning(void)
{
    return s_running;
}
