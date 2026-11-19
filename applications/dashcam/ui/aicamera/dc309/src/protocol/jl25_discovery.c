/*
 * Legacy APP discovery/status notification.
 *
 * The production netctrl implementation notifies the APP on UDP:49142 with
 * CAM_NUM_CHANGE and SD_STATUS messages.  The APP uses this notification to
 * start or restore its TCP control connection after joining the camera AP.
 */
#define DEBUG
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "jl25_discovery.h"
#include "jl25_internal.h"
#include "mlog.h"
#include "mode.h"
#include "param.h"

#define JL25_DISCOVERY_PORT 49142
#define JL25_DISCOVERY_BROADCAST "192.168.169.255"
#define JL25_DISCOVERY_INTERVAL_MS 500

static pthread_mutex_t s_discovery_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_discovery_thread;
static int s_discovery_started;
static int s_discovery_fd = -1;
static struct sockaddr_in s_discovery_peer;
static int s_discovery_peer_valid;

static void jl25_discovery_build_message(char* buf, size_t buf_sz)
{
    PARAM_MENU_S menu = { 0 };
    int cam_num = 1;
    const char* sd_status;

    PARAM_GetMenuParam(&menu);
    if (((menu.ViewWin.Current >> 1) & 0x1) == 1) {
        cam_num = 2;
    }
    sd_status = MODEMNG_GetCardState() == CARD_STATE_AVAILABLE ? "ON" : "OFF";

    snprintf(buf, buf_sz,
        "CAM_NUM_CHANGE = %d\r\nSD_STATUS = %s\r\n",
        cam_num, sd_status);
}

static void jl25_discovery_send(const char* message,
    const struct sockaddr_in* peer, int peer_valid)
{
    struct sockaddr_in broadcast;
    ssize_t sent;

    memset(&broadcast, 0, sizeof(broadcast));
    broadcast.sin_family = AF_INET;
    broadcast.sin_port = htons(JL25_DISCOVERY_PORT);
    broadcast.sin_addr.s_addr = inet_addr(JL25_DISCOVERY_BROADCAST);

    sent = sendto(s_discovery_fd, message, strlen(message), 0,
        (const struct sockaddr*)&broadcast, sizeof(broadcast));
    if (sent < 0) {
        MLOG_DBG("jl25 discovery broadcast send fail, errno=%d\n", errno);
    }

    /*
     * Once the APP has issued an HTTP request, also send directly to its
     * address.  This matches the production implementation and avoids
     * relying solely on broadcast delivery on some phone platforms.
     */
    if (peer_valid && peer->sin_addr.s_addr != broadcast.sin_addr.s_addr) {
        sent = sendto(s_discovery_fd, message, strlen(message), 0,
            (const struct sockaddr*)peer, sizeof(*peer));
        if (sent < 0) {
            MLOG_DBG("jl25 discovery peer send fail, errno=%d\n", errno);
        }
    }
}

static void* jl25_discovery_thread_fn(void* arg)
{
    int last_cam_num = -1;
    int last_card_state = -1;

    (void)arg;

    while (1) {
        char message[128];
        struct sockaddr_in peer;
        int peer_valid;
        int cam_num;
        int card_state;
        int app_connected;
        int running;

        pthread_mutex_lock(&s_discovery_mutex);
        running = s_discovery_started;
        peer = s_discovery_peer;
        peer_valid = s_discovery_peer_valid;
        pthread_mutex_unlock(&s_discovery_mutex);
        if (!running) {
            break;
        }

        PARAM_MENU_S menu = { 0 };
        PARAM_GetMenuParam(&menu);
        cam_num = (((menu.ViewWin.Current >> 1) & 0x1) == 1) ? 2 : 1;
        card_state = MODEMNG_GetCardState();
        app_connected = jl25_appsock_has_client();

        /*
         * Before TCP:6035 is connected, repeat the initial notification so
         * the APP can receive it while the WiFi route is becoming usable.
         * After connection, only state changes are sent.
         */
        if (!app_connected || cam_num != last_cam_num || card_state != last_card_state) {
            jl25_discovery_build_message(message, sizeof(message));
            jl25_discovery_send(message, &peer, peer_valid);
            last_cam_num = cam_num;
            last_card_state = card_state;
        }
        usleep(JL25_DISCOVERY_INTERVAL_MS * 1000);
    }
    return NULL;
}

int jl25_discovery_start(void)
{
    int opt = 1;
    struct sockaddr_in local;

    pthread_mutex_lock(&s_discovery_mutex);
    if (s_discovery_started) {
        pthread_mutex_unlock(&s_discovery_mutex);
        return 0;
    }

    s_discovery_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_discovery_fd < 0) {
        MLOG_ERR("jl25 discovery socket fail, errno=%d\n", errno);
        pthread_mutex_unlock(&s_discovery_mutex);
        return -1;
    }
    if (setsockopt(s_discovery_fd, SOL_SOCKET, SO_BROADCAST,
            &opt, sizeof(opt))
        != 0) {
        MLOG_ERR("jl25 discovery broadcast option fail, errno=%d\n", errno);
        close(s_discovery_fd);
        s_discovery_fd = -1;
        pthread_mutex_unlock(&s_discovery_mutex);
        return -1;
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);
    if (bind(s_discovery_fd, (struct sockaddr*)&local, sizeof(local)) != 0) {
        MLOG_ERR("jl25 discovery bind fail, errno=%d\n", errno);
        close(s_discovery_fd);
        s_discovery_fd = -1;
        pthread_mutex_unlock(&s_discovery_mutex);
        return -1;
    }

    memset(&s_discovery_peer, 0, sizeof(s_discovery_peer));
    s_discovery_peer_valid = 0;
    s_discovery_started = 1;
    if (pthread_create(&s_discovery_thread, NULL,
            jl25_discovery_thread_fn, NULL)
        != 0) {
        s_discovery_started = 0;
        close(s_discovery_fd);
        s_discovery_fd = -1;
        pthread_mutex_unlock(&s_discovery_mutex);
        return -1;
    }
    MLOG_INFO("jl25 discovery started udp:%d broadcast=%s\n",
        JL25_DISCOVERY_PORT, JL25_DISCOVERY_BROADCAST);
    pthread_mutex_unlock(&s_discovery_mutex);
    return 0;
}

void jl25_discovery_stop(void)
{
    pthread_mutex_lock(&s_discovery_mutex);
    if (!s_discovery_started) {
        pthread_mutex_unlock(&s_discovery_mutex);
        return;
    }
    s_discovery_started = 0;
    if (s_discovery_fd >= 0) {
        close(s_discovery_fd);
        s_discovery_fd = -1;
    }
    pthread_mutex_unlock(&s_discovery_mutex);

    pthread_join(s_discovery_thread, NULL);
    MLOG_INFO("jl25 discovery stopped\n");
}

void jl25_discovery_note_peer(int fd)
{
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    char ip[INET_ADDRSTRLEN];

    if (getpeername(fd, (struct sockaddr*)&peer, &peer_len) != 0 || peer.sin_family != AF_INET) {
        return;
    }

    pthread_mutex_lock(&s_discovery_mutex);
    s_discovery_peer = peer;
    s_discovery_peer.sin_port = htons(JL25_DISCOVERY_PORT);
    s_discovery_peer_valid = 1;
    pthread_mutex_unlock(&s_discovery_mutex);

    if (inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)) != NULL) {
        MLOG_INFO("jl25 discovery peer learned ip=%s\n", ip);
    }
}
