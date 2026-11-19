/*
 * Storage serialization for the dc309 JL25 protocol adapter.
 *
 * Media enumeration/downloads must not run while the mode manager is
 * unmounting, formatting, and rebuilding FILEMNG state.
 */
#define DEBUG
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "filemng.h"
#include "jl25_internal.h"
#include "mlog.h"
#include "mode.h"

static pthread_rwlock_t s_jl25_storage_lock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t s_jl25_format_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_jl25_format_cond = PTHREAD_COND_INITIALIZER;
static int s_jl25_format_active;
static int s_jl25_format_done;
static int s_jl25_format_success;
static int s_jl25_format_scan_done;
static pthread_mutex_t s_jl25_rescan_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_jl25_rescan_running;

static int jl25_storage_card_usable(void)
{
    uint32_t card_state = MODEMNG_GetCardState();

    /*
     * CARD_STATE_FORMATED is still a mounted, usable card state in this
     * product.  FILEMNG owns the actual scan state, so do not require
     * MODEMNG to already report CARD_STATE_AVAILABLE here.
     */
    switch (card_state) {
    case CARD_STATE_REMOVE:
    case CARD_STATE_UNAVAILABLE:
    case CARD_STATE_ERROR:
    case CARD_STATE_FSERROR:
    case CARD_STATE_CHECKING:
    case CARD_STATE_MOUNT_FAILED:
        return 0;
    default:
        return 1;
    }
}

static int jl25_storage_format_is_active(void)
{
    int active;

    pthread_mutex_lock(&s_jl25_format_mutex);
    active = s_jl25_format_active;
    pthread_mutex_unlock(&s_jl25_format_mutex);
    return active;
}

/* VIIDURE V2.9, section 6.1.4 storage status mapping. */
int jl25_storage_get_app_status(void)
{
    if (jl25_storage_format_is_active()) {
        return 13; /* formatting */
    }

    switch (MODEMNG_GetCardState()) {
    case CARD_STATE_AVAILABLE:
    case CARD_STATE_FORMATED:
        return 0; /* normal */
    case CARD_STATE_REMOVE:
        return 2; /* not inserted */
    case CARD_STATE_UNAVAILABLE:
        return 1; /* unformatted */
    case CARD_STATE_ERROR:
        return 3; /* damaged */
    case CARD_STATE_FULL_SPACE:
        return 4; /* full */
    case CARD_STATE_READ_ONLY:
        return 10; /* locked / not writable */
    case CARD_STATE_SLOW:
        return 11; /* slow card */
    case CARD_STATE_FSERROR:
    case CARD_STATE_MOUNT_FAILED:
        return 12; /* abnormal, needs formatting */
    case CARD_STATE_CHECKING:
    default:
        return 99; /* unknown or transient state */
    }
}

int jl25_storage_access_begin(void)
{
    /*
	 * Do not queue HTTP workers behind a format operation.  The APP polls
	 * storage state while formatting; returning busy lets those requests
	 * finish without exhausting the embedded thread pool/stack.
	 */
    return pthread_rwlock_tryrdlock(&s_jl25_storage_lock);
}

void jl25_storage_access_end(void)
{
    (void)pthread_rwlock_unlock(&s_jl25_storage_lock);
}

void jl25_storage_format_event(uint32_t topic)
{
    pthread_mutex_lock(&s_jl25_format_mutex);
    if (!s_jl25_format_active) {
        pthread_mutex_unlock(&s_jl25_format_mutex);
        return;
    }

    MLOG_INFO("jl25: format event topic=%#x\n", topic);
    switch (topic) {
    case EVENT_MODEMNG_CARD_FORMAT_SUCCESSED:
        s_jl25_format_success = 1;
        if (s_jl25_format_scan_done) {
            s_jl25_format_done = 1;
        }
        break;
    case EVENT_FILEMNG_SCAN_COMPLETED:
        s_jl25_format_scan_done = 1;
        if (s_jl25_format_success) {
            s_jl25_format_done = 1;
        }
        break;
    case EVENT_MODEMNG_CARD_FORMAT_FAILED:
    case EVENT_MODEMNG_CARD_MOUNT_FAILED:
    case EVENT_FILEMNG_SCAN_FAIL:
        s_jl25_format_success = 0;
        s_jl25_format_done = 1;
        break;
    default:
        break;
    }

    if (s_jl25_format_done) {
        pthread_cond_broadcast(&s_jl25_format_cond);
    }
    pthread_mutex_unlock(&s_jl25_format_mutex);
}

static int jl25_storage_wait_format(int timeout_ms)
{
    struct timespec deadline;
    int ret = 0;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        pthread_mutex_lock(&s_jl25_format_mutex);
        s_jl25_format_active = 0;
        pthread_mutex_unlock(&s_jl25_format_mutex);
        return -1;
    }
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&s_jl25_format_mutex);
    while (!s_jl25_format_done) {
        ret = pthread_cond_timedwait(&s_jl25_format_cond,
            &s_jl25_format_mutex,
            &deadline);
        if (ret == ETIMEDOUT) {
            break;
        }
        if (ret != 0) {
            break;
        }
    }
    if (!s_jl25_format_done) {
        ret = -1;
    } else if (!s_jl25_format_success) {
        ret = -1;
    }
    s_jl25_format_active = 0;
    pthread_mutex_unlock(&s_jl25_format_mutex);
    return ret;
}

int jl25_storage_format_card(void)
{
    MESSAGE_S msg = { 0 };
    int ret;

    if (jl25_capture_stop_record() != 0) {
        MLOG_ERR("jl25: stop recording before format failed\n");
        return -1;
    }
    if (jl25_preview_stop() != 0) {
        MLOG_ERR("jl25: stop preview before format failed\n");
        return -1;
    }

    pthread_mutex_lock(&s_jl25_format_mutex);
    if (s_jl25_format_active) {
        pthread_mutex_unlock(&s_jl25_format_mutex);
        MLOG_ERR("jl25: format already active\n");
        return -1;
    }
    s_jl25_format_active = 1;
    s_jl25_format_done = 0;
    s_jl25_format_success = 0;
    s_jl25_format_scan_done = 0;
    pthread_mutex_unlock(&s_jl25_format_mutex);

    ret = pthread_rwlock_wrlock(&s_jl25_storage_lock);
    if (ret != 0) {
        pthread_mutex_lock(&s_jl25_format_mutex);
        s_jl25_format_active = 0;
        pthread_mutex_unlock(&s_jl25_format_mutex);
        return -1;
    }

    if (MODEMNG_GetCardState() != CARD_STATE_AVAILABLE) {
        MLOG_ERR("jl25: format rejected, card state=%d\n",
            MODEMNG_GetCardState());
        pthread_mutex_lock(&s_jl25_format_mutex);
        s_jl25_format_active = 0;
        pthread_mutex_unlock(&s_jl25_format_mutex);
        pthread_rwlock_unlock(&s_jl25_storage_lock);
        return -1;
    }

    msg.topic = EVENT_MODEMNG_CARD_FORMAT;
    ret = MODEMNG_SendMessage(&msg);
    if (ret != 0) {
        pthread_mutex_lock(&s_jl25_format_mutex);
        s_jl25_format_active = 0;
        pthread_mutex_unlock(&s_jl25_format_mutex);
        pthread_rwlock_unlock(&s_jl25_storage_lock);
        MLOG_ERR("jl25: send format message failed, ret=%d\n", ret);
        return -1;
    }

    ret = jl25_storage_wait_format(30000);
    pthread_rwlock_unlock(&s_jl25_storage_lock);
    if (ret != 0) {
        MLOG_ERR("jl25: format did not complete successfully, ret=%d\n", ret);
        return -1;
    }

    MLOG_INFO("jl25: format completed and file scan is ready\n");
    return 0;
}

/*
 * Rebuild FILEMNG/DTCF indexes after the APP reconnects.  FILEMNG has no
 * public synchronous scan API; toggling NOT_AVAILABLE -> AVAILABLE is the
 * same transition used after mounting the card and makes its worker run
 * DTCF_Scan() again.
 */
static void* jl25_storage_rescan_thread(void* arg)
{
    int scan_requested = 0;

    (void)arg;

    /*
     * If the card is still in the initial AVAILABLE state, let the normal
     * mount scan finish instead of interrupting it.  A rescan is only needed
     * after FILEMNG has reached SCAN_COMPLETED.
     */
    for (int i = 0; i < 120; i++) {
        int status;

        if (jl25_storage_format_is_active() || !jl25_storage_card_usable() || jl25_capture_is_recording()) {
            goto EXIT;
        }
        status = FILEMNG_GetStorageStatus();
        if (status == FILEMNG_STORAGE_STATE_SCAN_COMPLETED) {
            break;
        }
        usleep(100 * 1000);
    }

    if (FILEMNG_GetStorageStatus() != FILEMNG_STORAGE_STATE_SCAN_COMPLETED || jl25_storage_format_is_active() || !jl25_storage_card_usable() || jl25_capture_is_recording()) {
        MLOG_INFO("jl25: active file scan skipped, card=%u storage=%d recording=%d\n",
            MODEMNG_GetCardState(), FILEMNG_GetStorageStatus(),
            jl25_capture_is_recording());
        goto EXIT;
    }

    if (pthread_rwlock_wrlock(&s_jl25_storage_lock) != 0) {
        goto EXIT;
    }

    if (!jl25_storage_format_is_active() && jl25_storage_card_usable() && !jl25_capture_is_recording() && FILEMNG_GetStorageStatus() == FILEMNG_STORAGE_STATE_SCAN_COMPLETED) {
        MLOG_INFO("jl25: APP connected, active file scan start\n");
        FILEMNG_SetStorageStatus(FILEMNG_STORAGE_STATE_NOT_AVAILABLE);
        scan_requested = 1;

        /*
         * FILEMNG checks this state every 500 ms.  Keep it asserted for a
         * full worker interval before requesting the new scan.
         */
        usleep(700 * 1000);
        if (!jl25_storage_format_is_active() && jl25_storage_card_usable()) {
            FILEMNG_SetStorageStatus(FILEMNG_STORAGE_STATE_AVAILABLE);
            scan_requested = 0;
            if (!jl25_capture_is_recording()) {
                for (int i = 0; i < 120; i++) {
                    if (FILEMNG_GetStorageStatus() == FILEMNG_STORAGE_STATE_SCAN_COMPLETED) {
                        MLOG_INFO("jl25: APP active file scan completed\n");
                        break;
                    }
                    if (jl25_storage_format_is_active() || !jl25_storage_card_usable() || jl25_capture_is_recording()) {
                        break;
                    }
                    usleep(100 * 1000);
                }
            }
        }
    }
    pthread_rwlock_unlock(&s_jl25_storage_lock);

EXIT:
    /*
     * Never leave FILEMNG in NOT_AVAILABLE if the card disappears or
     * recording starts during the trigger window.
     */
    if (scan_requested) {
        if (jl25_storage_card_usable()) {
            FILEMNG_SetStorageStatus(FILEMNG_STORAGE_STATE_AVAILABLE);
        }
        MLOG_INFO("jl25: active file scan trigger aborted, card=%u storage=%d\n",
            MODEMNG_GetCardState(), FILEMNG_GetStorageStatus());
    }
    pthread_mutex_lock(&s_jl25_rescan_mutex);
    s_jl25_rescan_running = 0;
    pthread_mutex_unlock(&s_jl25_rescan_mutex);
    return NULL;
}

void jl25_storage_request_scan(void)
{
    pthread_t tid;
    pthread_attr_t attr;

    pthread_mutex_lock(&s_jl25_rescan_mutex);
    if (s_jl25_rescan_running) {
        pthread_mutex_unlock(&s_jl25_rescan_mutex);
        return;
    }
    s_jl25_rescan_running = 1;
    pthread_mutex_unlock(&s_jl25_rescan_mutex);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, jl25_storage_rescan_thread, NULL) != 0) {
        pthread_mutex_lock(&s_jl25_rescan_mutex);
        s_jl25_rescan_running = 0;
        pthread_mutex_unlock(&s_jl25_rescan_mutex);
        MLOG_ERR("jl25: create active file scan thread failed\n");
    }
    pthread_attr_destroy(&attr);
}
