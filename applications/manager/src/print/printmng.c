#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "printmng.h"
#include "print_motor.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker;
    uint32_t initialized;
    uint32_t closing;
    uint64_t next_id;
    PRINTMNG_Config config;
    PRINTMNG_Job job;
    PRINTMNG_Status status;
} printer = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static pthread_once_t condition_once = PTHREAD_ONCE_INIT;
static int condition_error;

static void initialize_condition(void)
{
    pthread_condattr_t attributes;
    condition_error = pthread_condattr_init(&attributes);
    if (condition_error)
        return;
    condition_error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (!condition_error)
        condition_error = pthread_cond_init(&printer.changed, &attributes);
    pthread_condattr_destroy(&attributes);
}

static uint64_t monotonic_ms(void)
{
    struct timespec instant;
    clock_gettime(CLOCK_MONOTONIC, &instant);
    return (uint64_t)instant.tv_sec * 1000U + (uint64_t)instant.tv_nsec / 1000000U;
}

static struct timespec deadline_after(uint32_t milliseconds)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += milliseconds / 1000U;
    deadline.tv_nsec += (long)(milliseconds % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

static int backend_result(int result)
{
    return result > 0 ? -EIO : result;
}

static void emit_status(void)
{
    if (!printer.config.on_status)
        return;
    PRINTMNG_Status status;
    pthread_mutex_lock(&printer.mutex);
    status = printer.status;
    pthread_mutex_unlock(&printer.mutex);
    printer.config.on_status(&status, printer.config.status_context);
}

static void transition(PRINTMNG_State state)
{
    pthread_mutex_lock(&printer.mutex);
    printer.status.state = state;
    pthread_cond_broadcast(&printer.changed);
    pthread_mutex_unlock(&printer.mutex);
    emit_status();
}

static void tick(void)
{
    struct timespec deadline = deadline_after(printer.config.poll_ms);
    pthread_mutex_lock(&printer.mutex);
    if (!printer.status.cancel_requested && !printer.closing)
        pthread_cond_timedwait(&printer.changed, &printer.mutex, &deadline);
    pthread_mutex_unlock(&printer.mutex);
}

static int checkpoint(void)
{
    pthread_mutex_lock(&printer.mutex);
    int cancelled = printer.status.cancel_requested || printer.closing;
    pthread_mutex_unlock(&printer.mutex);
    if (cancelled)
        return -ECANCELED;
    const PRINTMNG_Backend *backend = &printer.config.backend;
    if (!backend->check_safety)
        return 0;
    uint32_t faults = 0;
    int result = backend_result(backend->check_safety(backend->context, &faults));
    pthread_mutex_lock(&printer.mutex);
    printer.status.safety_faults = faults;
    pthread_mutex_unlock(&printer.mutex);
    return result < 0 ? result : faults ? -EIO : 0;
}

static int delay(uint32_t milliseconds)
{
    uint64_t started = monotonic_ms();
    for (;;) {
        int result = checkpoint();
        if (result < 0)
            return result;
        if (monotonic_ms() - started >= milliseconds)
            return 0;
        tick();
    }
}

static uint32_t feed_offset(const PRINTMNG_Job *job, uint32_t sweeps)
{
    return ((uint64_t)sweeps * job->head_width_um + job->passes / 2U) / job->passes;
}

void PRINTMNG_DefaultJob(PRINTMNG_Job *job)
{
    if (!job)
        return;
    memset(job, 0, sizeof(*job));
    job->passes = 8;
    job->scan_width_um = 44000;
    job->feed_length_um = 60000;
    job->head_width_um = 8128;
    job->nozzle_pitch_nm = 42330;
    job->prefeed_um = 15000;
    job->scan_travel_um = 60000;
    job->data_start_um[0] = 5500;
    job->data_start_um[1] = 5500;
}

void PRINTMNG_DefaultConfig(PRINTMNG_Config *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->mode = PRINTMNG_DRY_RUN;
    config->scan_axis.channel = MOTORMNG_CH12;
    config->feed_axis.channel = MOTORMNG_CH34;
    config->scan_axis.pps = MOTORMNG_DEFAULT_PPS;
    config->feed_axis.pps = MOTORMNG_DEFAULT_PPS;
    config->scan_axis.current_estimate_ma = MOTORMNG_DEFAULT_CURRENT_ESTIMATE_MA;
    config->feed_axis.current_estimate_ma = MOTORMNG_DEFAULT_CURRENT_ESTIMATE_MA;
    config->poll_ms = 10;
    config->settle_ms = 100;
    config->simulated_scan_ms = 80;
    config->simulated_feed_ms = 30;
    config->move_timeout_ms = 10000;
    config->home_timeout_ms = 8000;
}

int PRINTMNG_CalculatePlan(const PRINTMNG_Job *job, PRINTMNG_Plan *plan)
{
    if (!job || !plan)
        return -EINVAL;
    memset(plan, 0, sizeof(*plan));
    if (!memchr(job->image_path, '\0', sizeof(job->image_path)) ||
        (job->passes != 8 && job->passes != 16) || !job->scan_width_um ||
        job->scan_width_um > 300000 || !job->feed_length_um || job->feed_length_um > 1000000 ||
        job->head_width_um < job->passes || job->head_width_um > 50000 ||
        !job->nozzle_pitch_nm || job->nozzle_pitch_nm > 1000000 ||
        job->prefeed_um > 1000000 || job->eject_um > 1000000 ||
        !job->scan_travel_um || job->scan_travel_um > 1000000)
        return -EINVAL;
    for (unsigned int index = 0; index < 2; ++index) {
        if ((uint64_t)job->data_start_um[index] + job->scan_width_um > job->scan_travel_um)
            return -ERANGE;
    }
    uint64_t columns = ((uint64_t)job->scan_width_um * 1000U * (job->passes / 2U) +
                        job->nozzle_pitch_nm / 2U) / job->nozzle_pitch_nm;
    uint64_t rows = ((uint64_t)job->feed_length_um * 1000U + job->nozzle_pitch_nm / 2U) /
                     job->nozzle_pitch_nm;
    uint64_t sweeps = ((uint64_t)job->feed_length_um * job->passes + job->head_width_um - 1U) /
                       job->head_width_um;
    if (!columns || columns > 1000000 || !rows || rows > 1000000 || sweeps > 10000)
        return -ERANGE;
    plan->sweeps = sweeps;
    plan->columns_per_sweep = columns;
    plan->image_rows = rows;
    plan->total_columns = columns * sweeps;
    plan->total_feed_um = job->prefeed_um + feed_offset(job, sweeps) + job->eject_um;
    return 0;
}

static int validate_axis(const PRINTMNG_AxisConfig *axis)
{
    if (axis->enabled > 1)
        return -EINVAL;
    if (!axis->enabled)
        return 0;
    if ((axis->channel != MOTORMNG_CH12 && axis->channel != MOTORMNG_CH34) ||
        (axis->forward_direction != MOTORMNG_FORWARD && axis->forward_direction != MOTORMNG_REVERSE) ||
        axis->pps < MOTORMNG_START_PPS || axis->pps > MOTORMNG_MAX_PPS ||
        (axis->current_estimate_ma != 100 && axis->current_estimate_ma != 125 &&
         axis->current_estimate_ma != 150))
        return -EINVAL;
    return axis->travel_um_per_motor_rev ? 0 : -ENODATA;
}

static int validate_config(const PRINTMNG_Config *config)
{
    if (config->mode < PRINTMNG_DRY_RUN || config->mode > PRINTMNG_REAL_PRINT ||
        config->i2c_bus > 3 || !config->poll_ms || config->poll_ms > 1000 ||
        config->settle_ms > 60000 || !config->simulated_scan_ms || config->simulated_scan_ms > 60000 ||
        !config->simulated_feed_ms || config->simulated_feed_ms > 60000 ||
        !config->move_timeout_ms || config->move_timeout_ms > PRINTMNG_MAX_WAIT_MS ||
        !config->home_timeout_ms || config->home_timeout_ms > PRINTMNG_MAX_WAIT_MS)
        return -EINVAL;
    int result = validate_axis(&config->scan_axis);
    if (!result)
        result = validate_axis(&config->feed_axis);
    if (result < 0)
        return result;
    uint32_t enabled = config->scan_axis.enabled + config->feed_axis.enabled;
    if ((config->mode == PRINTMNG_DRY_RUN && enabled) ||
        (config->mode != PRINTMNG_DRY_RUN && !enabled) ||
        (enabled == 2 && config->scan_axis.channel == config->feed_axis.channel))
        return -EINVAL;
    const PRINTMNG_Backend *backend = &config->backend;
    unsigned int homing = !!backend->home_begin + !!backend->home_poll + !!backend->home_end;
    if (homing && (homing != 3 || !config->scan_axis.enabled))
        return -EINVAL;
    if (config->allow_unhomed_scan > 1)
        return -EINVAL;
    if (config->mode == PRINTMNG_MOTOR_SIMULATION && config->scan_axis.enabled &&
        !homing && !config->allow_unhomed_scan)
        return -ENOTSUP;
    if (config->mode == PRINTMNG_REAL_PRINT &&
        (enabled != 2 || homing != 3 || !backend->check_safety || !backend->prepare_image ||
         !backend->arm_sweep || !backend->poll_sweep || !backend->end_sweep ||
         !backend->finish || !backend->abort))
        return -ENOTSUP;
    return 0;
}

static int home_scan(void)
{
    transition(PRINTMNG_HOMING);
    const PRINTMNG_Backend *backend = &printer.config.backend;
    const PRINTMNG_AxisConfig *axis = &printer.config.scan_axis;
    int result = checkpoint();
    if (result < 0)
        return result;
    if (!backend->home_begin) {
        result = delay(printer.config.simulated_feed_ms);
    } else {
        MOTORMNG_Direction left = axis->forward_direction == MOTORMNG_FORWARD ?
                                 MOTORMNG_REVERSE : MOTORMNG_FORWARD;
        result = backend_result(backend->home_begin(backend->context, axis->channel, left));
        uint64_t started = monotonic_ms();
        uint32_t complete = 0;
        while (!result && !complete) {
            result = checkpoint();
            if (!result)
                result = backend_result(backend->home_poll(backend->context, &complete));
            if (!result && !complete) {
                if (monotonic_ms() - started >= printer.config.home_timeout_ms)
                    result = -ETIMEDOUT;
                else
                    tick();
            }
        }
        int stopped = backend_result(backend->home_end(backend->context));
        if (!result)
            result = stopped;
        if (!result) {
            pthread_mutex_lock(&printer.mutex);
            printer.status.physical_home_verified = 1;
            pthread_mutex_unlock(&printer.mutex);
        }
    }
    if (!result)
        result = print_motor_reference(axis);
    return result;
}

static void set_columns(uint32_t columns, uint64_t previous)
{
    pthread_mutex_lock(&printer.mutex);
    printer.status.columns_done = columns;
    printer.status.total_columns_done = previous + columns;
    pthread_mutex_unlock(&printer.mutex);
}

static int move_axis(const PRINTMNG_AxisConfig *axis, uint32_t target_um,
                     uint32_t duration_ms, const PRINTMNG_Sweep *sweep)
{
    int result = checkpoint();
    if (result < 0)
        return result;
    if (axis->enabled) {
        result = print_motor_move(axis, target_um);
        if (result < 0)
            return result;
    }
    uint64_t started = monotonic_ms();
    uint64_t previous_columns = sweep ? (uint64_t)(sweep->index - 1U) * sweep->columns : 0;
    uint32_t columns = 0;
    for (;;) {
        result = checkpoint();
        if (result < 0)
            return result;
        uint64_t elapsed = monotonic_ms() - started;
        int complete = elapsed >= duration_ms;
        int64_t position = 0;
        if (axis->enabled) {
            result = print_motor_poll(axis, &complete, &position);
            if (result < 0)
                return result;
        }
        if (sweep && printer.config.mode == PRINTMNG_REAL_PRINT) {
            PRINTMNG_HeadStatus head = {0};
            result = backend_result(printer.config.backend.poll_sweep(printer.config.backend.context, &head));
            if (result < 0)
                return result;
            if (head.columns_sent < columns || head.columns_sent > sweep->columns ||
                (head.complete && head.columns_sent != sweep->columns))
                return -EPROTO;
            columns = head.columns_sent;
            set_columns(columns, previous_columns);
            if (complete && (!head.complete || columns != sweep->columns))
                return -ENODATA;
        } else if (sweep) {
            uint64_t progress_um;
            if (axis->enabled) {
                if (position < 0)
                    position = -position;
                uint64_t from_left = (uint64_t)position * axis->travel_um_per_motor_rev /
                                     (MOTORMNG_FULL_STEPS_PER_REV * MOTORMNG_MICROSTEPS_PER_FULL_STEP);
                progress_um = sweep->direction == PRINTMNG_LEFT_TO_RIGHT ? from_left :
                              from_left >= printer.job.scan_travel_um ? 0 : printer.job.scan_travel_um - from_left;
            } else {
                progress_um = (elapsed < duration_ms ? elapsed : duration_ms) * printer.job.scan_travel_um / duration_ms;
            }
            if (complete)
                progress_um = printer.job.scan_travel_um;
            uint64_t fed = progress_um <= sweep->data_start_um ? 0 :
                           (progress_um - sweep->data_start_um) * sweep->columns / printer.job.scan_width_um;
            columns = fed > sweep->columns ? sweep->columns : fed;
            set_columns(columns, previous_columns);
        }
        if (complete)
            return sweep && columns != sweep->columns ? -ENODATA : 0;
        if (elapsed >= printer.config.move_timeout_ms)
            return -ETIMEDOUT;
        tick();
    }
}

static int feed_to(uint32_t target_um, PRINTMNG_State state)
{
    pthread_mutex_lock(&printer.mutex);
    uint32_t current = printer.status.feed_commanded_um;
    pthread_mutex_unlock(&printer.mutex);
    if (target_um == current)
        return checkpoint();
    transition(state);
    int result = move_axis(&printer.config.feed_axis, target_um, printer.config.simulated_feed_ms, NULL);
    if (!result) {
        pthread_mutex_lock(&printer.mutex);
        printer.status.feed_commanded_um = target_um;
        pthread_mutex_unlock(&printer.mutex);
        emit_status();
    }
    return result;
}

static int run_job(const PRINTMNG_Job *job, const PRINTMNG_Plan *plan)
{
    const PRINTMNG_Backend *backend = &printer.config.backend;
    transition(PRINTMNG_PREPARING);
    int result = checkpoint();
    if (!result && printer.config.mode == PRINTMNG_REAL_PRINT)
        result = backend_result(backend->prepare_image(backend->context, job, plan));
    if (!result)
        result = checkpoint();
    if (!result)
        result = print_motor_reference(&printer.config.feed_axis);
    if (!result)
        result = feed_to(job->prefeed_um, PRINTMNG_PREFEED);
    if (!result)
        result = home_scan();
    for (uint32_t index = 0; !result && index < plan->sweeps; ++index) {
        PRINTMNG_Sweep sweep = {
            .index = index + 1U,
            .total = plan->sweeps,
            .interlace_phase = index % job->passes,
            .direction = index % 2U ? PRINTMNG_RIGHT_TO_LEFT : PRINTMNG_LEFT_TO_RIGHT,
            .columns = plan->columns_per_sweep,
            .feed_offset_um = feed_offset(job, index)
        };
        sweep.data_start_um = job->data_start_um[sweep.direction];
        sweep.data_end_um = sweep.data_start_um + job->scan_width_um;
        pthread_mutex_lock(&printer.mutex);
        printer.status.sweep = sweep;
        printer.status.columns_done = 0;
        pthread_mutex_unlock(&printer.mutex);
        transition(PRINTMNG_SETTLING);
        result = delay(printer.config.settle_ms);
        if (!result && printer.config.mode == PRINTMNG_REAL_PRINT)
            result = backend_result(backend->arm_sweep(backend->context, &sweep));
        if (!result) {
            transition(PRINTMNG_SCANNING);
            result = move_axis(&printer.config.scan_axis,
                               sweep.direction == PRINTMNG_LEFT_TO_RIGHT ? job->scan_travel_um : 0,
                               printer.config.simulated_scan_ms, &sweep);
        }
        if (!result && printer.config.mode == PRINTMNG_REAL_PRINT)
            result = backend_result(backend->end_sweep(backend->context));
        if (!result) {
            pthread_mutex_lock(&printer.mutex);
            printer.status.sweeps_done = index + 1U;
            pthread_mutex_unlock(&printer.mutex);
            transition(PRINTMNG_SETTLING);
            result = delay(printer.config.settle_ms);
        }
        if (!result)
            result = feed_to(job->prefeed_um + feed_offset(job, index + 1U), PRINTMNG_FEEDING);
    }
    if (!result && job->eject_um)
        result = feed_to(plan->total_feed_um, PRINTMNG_EJECTING);
    if (!result)
        result = checkpoint();
    if (!result && printer.config.mode == PRINTMNG_REAL_PRINT)
        result = backend_result(backend->finish(backend->context));
    return result;
}

static void *print_worker(void *argument)
{
    (void)argument;
    pthread_mutex_lock(&printer.mutex);
    for (;;) {
        while (!printer.status.active && !printer.closing)
            pthread_cond_wait(&printer.changed, &printer.mutex);
        if (!printer.status.active && printer.closing)
            break;
        PRINTMNG_Job job = printer.job;
        PRINTMNG_Plan plan = printer.status.plan;
        pthread_mutex_unlock(&printer.mutex);
        int result = run_job(&job, &plan);
        pthread_mutex_lock(&printer.mutex);
        if (!result && (printer.status.cancel_requested || printer.closing))
            result = -ECANCELED;
        pthread_mutex_unlock(&printer.mutex);
        int cleanup = 0;
        if (result < 0 && printer.config.mode == PRINTMNG_REAL_PRINT)
            cleanup = backend_result(printer.config.backend.abort(printer.config.backend.context));
        int stopped = print_motor_stop(&printer.config);
        if (!cleanup)
            cleanup = stopped;
        pthread_mutex_lock(&printer.mutex);
        if (!result && (printer.status.cancel_requested || printer.closing))
            result = -ECANCELED;
        printer.status.last_error = result;
        printer.status.cleanup_error = cleanup;
        printer.status.state = cleanup < 0 ? PRINTMNG_FAILED : result == -ECANCELED ?
                               PRINTMNG_CANCELLED : result < 0 ? PRINTMNG_FAILED : PRINTMNG_COMPLETED;
        if (!result && cleanup < 0)
            printer.status.last_error = cleanup;
        printer.status.active = 0;
        PRINTMNG_Status terminal = printer.status;
        pthread_cond_broadcast(&printer.changed);
        pthread_mutex_unlock(&printer.mutex);
        if (printer.config.on_status)
            printer.config.on_status(&terminal, printer.config.status_context);
        pthread_mutex_lock(&printer.mutex);
    }
    pthread_mutex_unlock(&printer.mutex);
    return NULL;
}

int PRINTMNG_Init(const PRINTMNG_Config *config)
{
    int once = pthread_once(&condition_once, initialize_condition);
    if (once || condition_error)
        return -(once ? once : condition_error);
    PRINTMNG_Config defaults;
    if (!config) {
        PRINTMNG_DefaultConfig(&defaults);
        config = &defaults;
    }
    int result = validate_config(config);
    if (result < 0)
        return result;
    pthread_mutex_lock(&printer.mutex);
    if (printer.initialized || printer.closing) {
        pthread_mutex_unlock(&printer.mutex);
        return -EALREADY;
    }
    result = print_motor_init(config);
    if (!result) {
        printer.config = *config;
        memset(&printer.status, 0, sizeof(printer.status));
        printer.status.mode = config->mode;
        printer.status.scan_motor_enabled = config->scan_axis.enabled;
        printer.status.feed_motor_enabled = config->feed_axis.enabled;
        printer.initialized = 1;
        int created = pthread_create(&printer.worker, NULL, print_worker, NULL);
        if (created) {
            printer.initialized = 0;
            print_motor_close(config);
            result = -created;
        }
    }
    pthread_mutex_unlock(&printer.mutex);
    return result;
}

int PRINTMNG_DeInit(void)
{
    pthread_mutex_lock(&printer.mutex);
    if (!printer.initialized || printer.closing) {
        int result = printer.closing ? -EBUSY : 0;
        pthread_mutex_unlock(&printer.mutex);
        return result;
    }
    if (pthread_equal(pthread_self(), printer.worker)) {
        pthread_mutex_unlock(&printer.mutex);
        return -EDEADLK;
    }
    printer.closing = 1;
    if (printer.status.active)
        printer.status.cancel_requested = 1;
    pthread_cond_broadcast(&printer.changed);
    pthread_mutex_unlock(&printer.mutex);
    int joined = pthread_join(printer.worker, NULL);
    int closed = print_motor_close(&printer.config);
    pthread_mutex_lock(&printer.mutex);
    int cleanup = printer.status.cleanup_error;
    printer.initialized = 0;
    printer.closing = 0;
    pthread_cond_broadcast(&printer.changed);
    pthread_mutex_unlock(&printer.mutex);
    return joined ? -joined : closed < 0 ? closed : cleanup;
}

int PRINTMNG_Submit(const PRINTMNG_Job *job, uint64_t *job_id)
{
    if (!job_id)
        return -EINVAL;
    *job_id = 0;
    PRINTMNG_Plan plan;
    int result = PRINTMNG_CalculatePlan(job, &plan);
    if (result < 0)
        return result;
    pthread_mutex_lock(&printer.mutex);
    if (!printer.initialized || printer.closing)
        result = -ENODEV;
    else if (printer.status.active)
        result = -EBUSY;
    else if (printer.status.cleanup_error || printer.status.state == PRINTMNG_FAILED)
        result = -EIO;
    else if (printer.config.mode == PRINTMNG_REAL_PRINT && !job->image_path[0])
        result = -EINVAL;
    if (!result)
        result = print_motor_validate(&printer.config.scan_axis, job->scan_travel_um);
    if (!result)
        result = print_motor_validate(&printer.config.feed_axis, job->prefeed_um);
    if (!result)
        result = print_motor_validate(&printer.config.feed_axis,
                                      (job->head_width_um + job->passes - 1U) / job->passes);
    if (!result && printer.config.feed_axis.enabled &&
        (uint64_t)(job->head_width_um / job->passes) * MOTORMNG_FULL_STEPS_PER_REV *
        MOTORMNG_MICROSTEPS_PER_FULL_STEP < printer.config.feed_axis.travel_um_per_motor_rev)
        result = -ERANGE;
    if (!result)
        result = print_motor_validate(&printer.config.feed_axis, job->eject_um);
    if (!result) {
        memset(&printer.status, 0, sizeof(printer.status));
        printer.job = *job;
        printer.status.job_id = ++printer.next_id;
        printer.status.mode = printer.config.mode;
        printer.status.scan_motor_enabled = printer.config.scan_axis.enabled;
        printer.status.feed_motor_enabled = printer.config.feed_axis.enabled;
        printer.status.state = PRINTMNG_QUEUED;
        printer.status.active = 1;
        printer.status.plan = plan;
        *job_id = printer.status.job_id;
        pthread_cond_broadcast(&printer.changed);
    }
    pthread_mutex_unlock(&printer.mutex);
    return result;
}

int PRINTMNG_Cancel(uint64_t job_id)
{
    pthread_mutex_lock(&printer.mutex);
    int result = !printer.initialized || printer.closing ? -ENODEV :
                 !job_id || job_id != printer.status.job_id ? -ENOENT : 0;
    if (!result && printer.status.active) {
        printer.status.cancel_requested = 1;
        pthread_cond_broadcast(&printer.changed);
    }
    pthread_mutex_unlock(&printer.mutex);
    return result;
}

int PRINTMNG_GetStatus(PRINTMNG_Status *status)
{
    if (!status)
        return -EINVAL;
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&printer.mutex);
    int result = !printer.initialized ? -ENODEV : 0;
    if (!result)
        *status = printer.status;
    pthread_mutex_unlock(&printer.mutex);
    return result;
}

int PRINTMNG_Wait(uint64_t job_id, uint32_t timeout_ms, PRINTMNG_Status *status)
{
    if (!job_id || !status || timeout_ms > PRINTMNG_MAX_WAIT_MS)
        return -EINVAL;
    memset(status, 0, sizeof(*status));
    struct timespec deadline = deadline_after(timeout_ms);
    pthread_mutex_lock(&printer.mutex);
    int result = !printer.initialized || printer.closing ? -ENODEV :
                 job_id != printer.status.job_id ? -ESTALE : 0;
    if (!result && pthread_equal(pthread_self(), printer.worker))
        result = -EDEADLK;
    while (!result && printer.status.active) {
        int waited = pthread_cond_timedwait(&printer.changed, &printer.mutex, &deadline);
        if (!printer.initialized || printer.closing)
            result = -ENODEV;
        else if (job_id != printer.status.job_id)
            result = -ESTALE;
        else if (waited)
            result = -waited;
    }
    if (job_id == printer.status.job_id) {
        *status = printer.status;
        if (!result)
            result = status->last_error;
    }
    pthread_mutex_unlock(&printer.mutex);
    return result;
}
