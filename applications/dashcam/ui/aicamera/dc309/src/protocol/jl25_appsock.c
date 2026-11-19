#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "jl25_internal.h"
#include "jl25_protocol.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"

#define JL25_APP_SOCKET_PORT 6035

static int s_appsock_listen_fd = -1;
static pthread_t s_appsock_accept_thread;
static int s_appsock_started = 0;
static int s_appsock_client_count = 0;
static JL25_APP_CONNECTION_CB s_app_connection_cb = NULL;
static JL25_APP_DISCONNECTION_CB s_app_disconnection_cb = NULL;
static pthread_mutex_t s_appsock_mutex = PTHREAD_MUTEX_INITIALIZER;

static void jl25_appsock_set_keepalive(int fd)
{
    int keepalive = 1;
    int keepidle = 3;
    int keepinterval = 1;
    int keepcount = 3;

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
    setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));
}

static int jl25_appsock_send_json(int fd, const char* json)
{
    return jl25_http_send_all(fd, json, strlen(json));
}

static void jl25_appsock_build_sd(char* buf, size_t buf_sz, int status)
{
    long long now = (long long)time(NULL);

    snprintf(buf, buf_sz,
        "{\"msgid\":\"sd\",\"info\":{\"status\":%d},\"time\":%lld}",
        status, now);
}

static void jl25_appsock_build_cam(char* buf, size_t buf_sz, uint32_t viewwin)
{
    int camnum = 1;
    int action = 0;
    int curcamid = 0;
    long long now = (long long)time(NULL);

    if (((viewwin >> 1) & 0x1) == 1) {
        camnum = 2;
        action = 1;
    }
    snprintf(buf, buf_sz,
        "{\"msgid\":\"cam_plugin\",\"info\":{\"action\":%d,"
        "\"curcamid\":%d,\"camnum\":%d},\"time\":%lld}",
        action, curcamid, camnum, now);
}

static void* jl25_appsock_client_thread(void* arg)
{
    int fd = (int)(intptr_t)arg;
    int last_card = -1;
    uint32_t last_viewwin = 0xffffffffU;

    prctl(PR_SET_NAME, "jl25AppSockCli", 0, 0, 0);
    jl25_appsock_set_keepalive(fd);

    while (JL25_IsRunning()) {
        PARAM_MENU_S menu = { 0 };
        char sd_json[128];
        char cam_json[160];
        int card_state = MODEMNG_GetCardState();

        PARAM_GetMenuParam(&menu);
        jl25_appsock_build_sd(sd_json, sizeof(sd_json),
            jl25_storage_get_app_status());
        if (jl25_appsock_send_json(fd, sd_json) != 0) {
            MLOG_INFO("jl25 appsock keepalive send fail, errno=%d\n", errno);
            break;
        }

        if (last_card != card_state || last_viewwin != menu.ViewWin.Current) {
            jl25_appsock_build_cam(cam_json, sizeof(cam_json),
                menu.ViewWin.Current);
            if (jl25_appsock_send_json(fd, cam_json) != 0) {
                MLOG_INFO("jl25 appsock cam send fail, errno=%d\n", errno);
                break;
            }
            last_card = card_state;
            last_viewwin = menu.ViewWin.Current;
        }
        usleep(1500 * 1000);
    }

    shutdown(fd, SHUT_RDWR);
    close(fd);
    pthread_mutex_lock(&s_appsock_mutex);
    if (s_appsock_client_count > 0) {
        s_appsock_client_count--;
    }
    pthread_mutex_unlock(&s_appsock_mutex);

    {
        JL25_APP_DISCONNECTION_CB callback;

        pthread_mutex_lock(&s_appsock_mutex);
        callback = s_app_disconnection_cb;
        pthread_mutex_unlock(&s_appsock_mutex);
        if (callback != NULL) {
            callback();
        }
    }
    return NULL;
}

static void* jl25_appsock_accept_thread_fn(void* arg)
{
    (void)arg;
    prctl(PR_SET_NAME, "jl25AppSock", 0, 0, 0);

    while (JL25_IsRunning()) {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int cfd = accept(s_appsock_listen_fd, (struct sockaddr*)&addr, &addrlen);

        if (cfd < 0) {
            if (JL25_IsRunning() && errno != EINTR) {
                MLOG_ERR("jl25 appsock accept fail, errno=%d\n", errno);
            }
            break;
        }

        MLOG_INFO("jl25 appsock client connected ip=[%s] port=%d\n",
            inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        jl25_storage_request_scan();
        MLOG_INFO("jl25 appsock active file scan requested\n");

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_mutex_lock(&s_appsock_mutex);
        s_appsock_client_count++;
        pthread_mutex_unlock(&s_appsock_mutex);
        if (pthread_create(&tid, &attr, jl25_appsock_client_thread,
                (void*)(intptr_t)cfd)
            != 0) {
            close(cfd);
            pthread_mutex_lock(&s_appsock_mutex);
            if (s_appsock_client_count > 0) {
                s_appsock_client_count--;
            }
            pthread_mutex_unlock(&s_appsock_mutex);
        } else {
            JL25_APP_CONNECTION_CB callback;

            pthread_mutex_lock(&s_appsock_mutex);
            callback = s_app_connection_cb;
            pthread_mutex_unlock(&s_appsock_mutex);
            if (callback != NULL) {
                callback();
            }
        }
        pthread_attr_destroy(&attr);
    }
    return NULL;
}

int jl25_appsock_start(void)
{
    int fd;
    int opt = 1;
    struct sockaddr_in addr;

    pthread_mutex_lock(&s_appsock_mutex);
    if (s_appsock_started) {
        pthread_mutex_unlock(&s_appsock_mutex);
        return 0;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        MLOG_ERR("jl25 appsock socket fail, errno=%d\n", errno);
        pthread_mutex_unlock(&s_appsock_mutex);
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(JL25_APP_SOCKET_PORT);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        MLOG_ERR("jl25 appsock bind port %d fail, errno=%d\n",
            JL25_APP_SOCKET_PORT, errno);
        close(fd);
        pthread_mutex_unlock(&s_appsock_mutex);
        return -1;
    }
    if (listen(fd, 4) != 0) {
        MLOG_ERR("jl25 appsock listen fail, errno=%d\n", errno);
        close(fd);
        pthread_mutex_unlock(&s_appsock_mutex);
        return -1;
    }
    s_appsock_listen_fd = fd;
    s_appsock_started = 1;
    s_appsock_client_count = 0;
    if (pthread_create(&s_appsock_accept_thread, NULL,
            jl25_appsock_accept_thread_fn, NULL)
        != 0) {
        close(s_appsock_listen_fd);
        s_appsock_listen_fd = -1;
        s_appsock_started = 0;
        pthread_mutex_unlock(&s_appsock_mutex);
        return -1;
    }
    MLOG_INFO("jl25 appsock started port=%d\n", JL25_APP_SOCKET_PORT);
    pthread_mutex_unlock(&s_appsock_mutex);
    return 0;
}

void jl25_appsock_stop(void)
{
    pthread_mutex_lock(&s_appsock_mutex);
    if (!s_appsock_started) {
        pthread_mutex_unlock(&s_appsock_mutex);
        return;
    }
    if (s_appsock_listen_fd >= 0) {
        shutdown(s_appsock_listen_fd, SHUT_RDWR);
        close(s_appsock_listen_fd);
        s_appsock_listen_fd = -1;
    }
    s_appsock_started = 0;
    pthread_mutex_unlock(&s_appsock_mutex);

    pthread_join(s_appsock_accept_thread, NULL);
    MLOG_INFO("jl25 appsock stopped\n");
}

int jl25_appsock_has_client(void)
{
    int connected;

    pthread_mutex_lock(&s_appsock_mutex);
    connected = s_appsock_client_count > 0;
    pthread_mutex_unlock(&s_appsock_mutex);
    return connected;
}

void JL25_SetAppConnectionCallback(JL25_APP_CONNECTION_CB callback)
{
    pthread_mutex_lock(&s_appsock_mutex);
    s_app_connection_cb = callback;
    pthread_mutex_unlock(&s_appsock_mutex);
}

void JL25_SetAppDisconnectionCallback(JL25_APP_DISCONNECTION_CB callback)
{
    pthread_mutex_lock(&s_appsock_mutex);
    s_app_disconnection_cb = callback;
    pthread_mutex_unlock(&s_appsock_mutex);
}