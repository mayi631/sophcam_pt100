#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "motormng.h"
#include "motor_hw.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

struct motor_axis {
    MOTORMNG_Status status;
    int64_t origin;
    uint32_t budget;
    uint32_t previous_raw;
    uint32_t ramp_initial;
    uint64_t ramp_started;
    uint64_t refreshed;
    uint64_t sequence;
};

static struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker;
    unsigned int initialized;
    unsigned int closing;
    uint64_t generation;
    MOTORMNG_Config config;
    struct motor_hw hardware;
    struct motor_axis axis[MOTORMNG_CHANNEL_COUNT];
} motor = {.mutex = PTHREAD_MUTEX_INITIALIZER};

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
        condition_error = pthread_cond_init(&motor.changed, &attributes);
    pthread_condattr_destroy(&attributes);
}

static uint64_t monotonic_ms(void)
{
    struct timespec instant;
    if (clock_gettime(CLOCK_MONOTONIC, &instant) < 0)
        return 0;
    return (uint64_t)instant.tv_sec * 1000U + (uint64_t)instant.tv_nsec / 1000000U;
}

static unsigned int speed_counter(unsigned int pps)
{
    return (28000000U + pps * 64U - 1U) / (pps * 64U);
}

static unsigned int agc_value(unsigned int current)
{
    return (uint64_t)current * 9300U * 128U / 5500000U;
}

static int axis_index(MOTORMNG_Channel channel)
{
    if (channel == MOTORMNG_CH12)
        return 0;
    if (channel == MOTORMNG_CH34)
        return 1;
    return -EINVAL;
}

static int ready_axis(MOTORMNG_Channel channel)
{
    int index = axis_index(channel);
    if (index < 0)
        return index;
    if (!motor.initialized || motor.closing)
        return -ENODEV;
    if (!motor.config.channel[index].enabled)
        return -ENODEV;
    return index;
}

static void fault_all(int error)
{
    for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
        if (!motor.config.channel[index].enabled)
            continue;
        motor_hw_stop(&motor.hardware, index);
        motor.axis[index].status.state = MOTORMNG_FAULT;
        motor.axis[index].status.stop_reason = MOTORMNG_STOP_FAULT;
        motor.axis[index].status.last_error = error < 0 ? error : -EIO;
        motor.axis[index].status.position_referenced = 0;
        motor.axis[index].status.command_pps = 0;
    }
    motor_hw_disable(&motor.hardware);
    pthread_cond_broadcast(&motor.changed);
}

static int sample_axis(unsigned int index, int *ended)
{
    struct motor_axis *axis = &motor.axis[index];
    uint32_t raw;
    int result = motor_hw_sample(&motor.hardware, index, &raw, ended);
    if (result < 0)
        return result;
    if (raw < axis->previous_raw || (axis->budget && raw > axis->budget))
        return -EIO;
    if ((axis->status.direction == MOTORMNG_FORWARD && axis->origin > INT64_MAX - (int64_t)raw) ||
        (axis->status.direction == MOTORMNG_REVERSE && axis->origin < INT64_MIN + (int64_t)raw))
        return -EOVERFLOW;
    axis->status.command_position_microsteps = axis->origin +
        (axis->status.direction == MOTORMNG_FORWARD ? (int64_t)raw : -(int64_t)raw);
    axis->previous_raw = raw;
    return 0;
}

static int stop_axis(unsigned int index, MOTORMNG_StopReason reason)
{
    struct motor_axis *axis = &motor.axis[index];
    if (axis->status.state == MOTORMNG_FAULT)
        return axis->status.last_error;
    if (axis->status.state != MOTORMNG_RUNNING)
        return 0;
    int result = motor_hw_stop(&motor.hardware, index);
    int ended;
    if (result == 0)
        result = sample_axis(index, &ended);
    if (result < 0) {
        fault_all(result);
        return result;
    }
    axis->status.state = reason == MOTORMNG_STOP_TARGET ? MOTORMNG_DONE : MOTORMNG_IDLE;
    axis->status.stop_reason = reason;
    axis->status.command_pps = 0;
    axis->status.speed_count = 0;
    pthread_cond_broadcast(&motor.changed);
    return 0;
}

static void service_axis(unsigned int index, uint64_t instant)
{
    struct motor_axis *axis = &motor.axis[index];
    if (axis->status.state != MOTORMNG_RUNNING)
        return;
    int ended;
    int result = sample_axis(index, &ended);
    if (result == 0 && ended) {
        if (!axis->budget || axis->previous_raw != axis->budget)
            result = -EIO;
        else {
            stop_axis(index, MOTORMNG_STOP_TARGET);
            return;
        }
    }
    unsigned int target = axis->status.target_pps;
    uint64_t elapsed = instant >= axis->ramp_started ? instant - axis->ramp_started : 0;
    uint64_t change = elapsed / 10U;
    unsigned int distance = target >= axis->ramp_initial ? target - axis->ramp_initial :
                                                          axis->ramp_initial - target;
    unsigned int pps = change >= distance ? target : target >= axis->ramp_initial ?
                       axis->ramp_initial + (unsigned int)change : axis->ramp_initial - (unsigned int)change;
    unsigned int count = speed_counter(pps);
    if (result == 0 && count != axis->status.speed_count)
        result = motor_hw_set_speed(&motor.hardware, index, count);
    if (result == 0) {
        axis->status.command_pps = pps;
        axis->status.speed_count = count;
    }
    if (result == 0 && !axis->budget && instant - axis->refreshed >= 1000U) {
        result = motor_hw_refresh(&motor.hardware, index);
        if (result == 0) {
            int64_t removed = axis->previous_raw & ~255U;
            axis->origin += axis->status.direction == MOTORMNG_FORWARD ? removed : -removed;
            axis->previous_raw &= 255U;
            axis->refreshed = instant;
            axis->status.position_referenced = 0;
        }
    }
    if (result < 0)
        fault_all(result);
}

static void *motor_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&motor.mutex);
    while (!motor.closing) {
        uint64_t instant = monotonic_ms();
        for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index)
            service_axis(index, instant);
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_nsec += 10000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&motor.changed, &motor.mutex, &deadline);
    }
    pthread_mutex_unlock(&motor.mutex);
    return NULL;
}

void MOTORMNG_DefaultConfig(MOTORMNG_Config *config)
{
    if (config) {
        memset(config, 0, sizeof(*config));
        for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
            config->channel[index].target_pps = MOTORMNG_DEFAULT_PPS;
            config->channel[index].current_estimate_ma = MOTORMNG_DEFAULT_CURRENT_ESTIMATE_MA;
        }
        config->channel[0].enabled = 1;
    }
}

int MOTORMNG_Init(const MOTORMNG_Config *config)
{
    int once_result = pthread_once(&condition_once, initialize_condition);
    if (once_result || condition_error)
        return -(once_result ? once_result : condition_error);
    MOTORMNG_Config defaults;
    if (!config) {
        MOTORMNG_DefaultConfig(&defaults);
        config = &defaults;
    }
    if (config->i2c_bus > 3 || (!config->channel[0].enabled && !config->channel[1].enabled))
        return -EINVAL;
    for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
        const MOTORMNG_ChannelConfig *axis = &config->channel[index];
        if (axis->enabled > 1 || axis->target_pps < 200 || axis->target_pps > 510 ||
            (axis->current_estimate_ma != 100 && axis->current_estimate_ma != 125 &&
             axis->current_estimate_ma != 150))
            return -EINVAL;
    }
    pthread_mutex_lock(&motor.mutex);
    int result = motor.initialized ? -EALREADY : 0;
    if (result == 0)
        result = motor_hw_open(&motor.hardware, config->i2c_bus);
    if (result == 0) {
        motor.config = *config;
        memset(motor.axis, 0, sizeof(motor.axis));
        for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
            motor.axis[index].status.target_pps = config->channel[index].target_pps;
            motor.axis[index].status.current_estimate_ma = config->channel[index].current_estimate_ma;
            motor.axis[index].status.agc = agc_value(config->channel[index].current_estimate_ma);
        }
        motor.closing = 0;
        motor.initialized = 1;
        ++motor.generation;
        int created = pthread_create(&motor.worker, NULL, motor_worker, NULL);
        if (created) {
            motor.initialized = 0;
            motor_hw_close(&motor.hardware);
            result = -created;
        }
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_DeInit(void)
{
    pthread_mutex_lock(&motor.mutex);
    if (!motor.initialized || motor.closing) {
        int result = motor.closing ? -EBUSY : 0;
        pthread_mutex_unlock(&motor.mutex);
        return result;
    }
    motor.closing = 1;
    int result = 0;
    for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
        if (motor.config.channel[index].enabled) {
            int stopped = stop_axis(index, MOTORMNG_STOP_REQUEST);
            if (stopped < 0)
                result = stopped;
        }
    }
    pthread_cond_broadcast(&motor.changed);
    pthread_mutex_unlock(&motor.mutex);
    int joined = pthread_join(motor.worker, NULL);
    pthread_mutex_lock(&motor.mutex);
    int closed = motor_hw_close(&motor.hardware);
    if (closed < 0)
        result = closed;
    motor.initialized = 0;
    motor.closing = 0;
    pthread_cond_broadcast(&motor.changed);
    pthread_mutex_unlock(&motor.mutex);
    return joined ? -joined : result;
}

static int start_axis(unsigned int index, MOTORMNG_Direction direction, uint32_t budget)
{
    struct motor_axis *axis = &motor.axis[index];
    if (direction != MOTORMNG_FORWARD && direction != MOTORMNG_REVERSE)
        return -EINVAL;
    if (axis->status.state == MOTORMNG_RUNNING)
        return -EBUSY;
    if (axis->status.state == MOTORMNG_FAULT)
        return axis->status.last_error;
    if ((direction == MOTORMNG_FORWARD && axis->status.positive_limit) ||
        (direction == MOTORMNG_REVERSE && axis->status.negative_limit))
        return -EPERM;
    int64_t distance = budget;
    if ((direction == MOTORMNG_FORWARD && axis->status.command_position_microsteps > INT64_MAX - distance) ||
        (direction == MOTORMNG_REVERSE && axis->status.command_position_microsteps < INT64_MIN + distance))
        return -EOVERFLOW;
    int result = motor_hw_begin(&motor.hardware, index, direction == MOTORMNG_REVERSE,
                                speed_counter(200), axis->status.agc, budget);
    if (result < 0) {
        fault_all(result);
        return result;
    }
    axis->origin = axis->status.command_position_microsteps;
    axis->budget = budget;
    axis->previous_raw = 0;
    axis->ramp_initial = 200;
    axis->ramp_started = monotonic_ms();
    axis->refreshed = axis->ramp_started;
    axis->status.state = MOTORMNG_RUNNING;
    ++axis->sequence;
    axis->status.stop_reason = MOTORMNG_STOP_NONE;
    axis->status.last_error = 0;
    axis->status.finite_move = !!budget;
    axis->status.direction = direction;
    axis->status.command_pps = 200;
    axis->status.speed_count = speed_counter(200);
    axis->status.target_position_microsteps = budget ? axis->origin +
        (direction == MOTORMNG_FORWARD ? distance : -distance) : axis->origin;
    pthread_cond_broadcast(&motor.changed);
    return 0;
}

int MOTORMNG_Start(MOTORMNG_Channel channel, MOTORMNG_Direction direction)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : start_axis((unsigned int)index, direction, 0);
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_Stop(MOTORMNG_Channel channel)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : stop_axis((unsigned int)index, MOTORMNG_STOP_REQUEST);
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_StopAll(void)
{
    pthread_mutex_lock(&motor.mutex);
    int result = !motor.initialized || motor.closing ? -ENODEV : 0;
    if (result == 0) {
        for (unsigned int index = 0; index < MOTORMNG_CHANNEL_COUNT; ++index) {
            if (motor.config.channel[index].enabled) {
                int stopped = stop_axis(index, MOTORMNG_STOP_REQUEST);
                if (stopped < 0)
                    result = stopped;
            }
        }
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

static int set_speed(unsigned int index, uint32_t pps)
{
    if (pps < 200 || pps > 510)
        return -EINVAL;
    struct motor_axis *axis = &motor.axis[index];
    if (axis->status.state == MOTORMNG_FAULT)
        return axis->status.last_error;
    axis->ramp_initial = axis->status.state == MOTORMNG_RUNNING ? axis->status.command_pps : 200;
    axis->ramp_started = monotonic_ms();
    axis->status.target_pps = pps;
    pthread_cond_broadcast(&motor.changed);
    return 0;
}

int MOTORMNG_SetSpeed(MOTORMNG_Channel channel, uint32_t pps)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : set_speed((unsigned int)index, pps);
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_SetLinearSpeedUm(MOTORMNG_Channel channel, uint32_t um_per_second)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    if (result == 0) {
        uint32_t travel = motor.config.channel[index].travel_um_per_motor_rev;
        if (!travel)
            result = -ENODATA;
        else {
            uint64_t pps = (uint64_t)um_per_second * 20U / travel;
            result = pps > 510 ? -EINVAL : set_speed((unsigned int)index, (uint32_t)pps);
        }
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

static int move_relative(unsigned int index, int64_t microsteps)
{
    if (!microsteps || microsteps > MOTORMNG_MAX_MOVE_MICROSTEPS ||
        microsteps < -(int64_t)MOTORMNG_MAX_MOVE_MICROSTEPS)
        return -EINVAL;
    MOTORMNG_Direction direction = microsteps > 0 ? MOTORMNG_FORWARD : MOTORMNG_REVERSE;
    uint32_t magnitude = (uint32_t)(microsteps > 0 ? microsteps : -microsteps);
    return start_axis(index, direction, magnitude);
}

int MOTORMNG_MoveRelative(MOTORMNG_Channel channel, int64_t microsteps)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : move_relative((unsigned int)index, microsteps);
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_MoveTo(MOTORMNG_Channel channel, int64_t position_microsteps)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    if (result == 0) {
        struct motor_axis *axis = &motor.axis[index];
        int64_t current = axis->status.command_position_microsteps;
        if (axis->status.state == MOTORMNG_RUNNING)
            result = -EBUSY;
        else if (axis->status.state == MOTORMNG_FAULT)
            result = axis->status.last_error;
        else if (!axis->status.position_referenced)
            result = -ENODATA;
        else if ((current < 0 && position_microsteps > INT64_MAX + current) ||
                 (current > 0 && position_microsteps < INT64_MIN + current))
            result = -EOVERFLOW;
        else if (position_microsteps != current)
            result = move_relative((unsigned int)index, position_microsteps - current);
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_MoveCyclesPhase(MOTORMNG_Channel channel, uint32_t cycles,
                            uint32_t phase, MOTORMNG_Direction direction)
{
    if (cycles > 65535 || phase >= 256 ||
        (direction != MOTORMNG_FORWARD && direction != MOTORMNG_REVERSE))
        return -EINVAL;
    int64_t microsteps = (int64_t)cycles * 256 + phase;
    return MOTORMNG_MoveRelative(channel, direction == MOTORMNG_FORWARD ? microsteps : -microsteps);
}

int MOTORMNG_MoveDistanceUm(MOTORMNG_Channel channel, int32_t distance_um)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    if (result == 0) {
        uint32_t travel = motor.config.channel[index].travel_um_per_motor_rev;
        if (!travel)
            result = -ENODATA;
        else {
            int64_t magnitude = distance_um >= 0 ? (int64_t)distance_um : -(int64_t)distance_um;
            int64_t microsteps = (magnitude * 1280 + travel / 2U) / travel;
            result = move_relative((unsigned int)index, distance_um >= 0 ? microsteps : -microsteps);
        }
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_SetPosition(MOTORMNG_Channel channel, int64_t position_microsteps)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    if (result == 0) {
        struct motor_axis *axis = &motor.axis[index];
        if (axis->status.state == MOTORMNG_RUNNING)
            result = -EBUSY;
        else if (axis->status.state == MOTORMNG_FAULT)
            result = axis->status.last_error;
        else {
            axis->status.command_position_microsteps = position_microsteps;
            axis->status.target_position_microsteps = position_microsteps;
            axis->status.position_referenced = 1;
        }
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_GetStatus(MOTORMNG_Channel channel, MOTORMNG_Status *status)
{
    if (!status)
        return -EINVAL;
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    if (index >= 0)
        *status = motor.axis[index].status;
    pthread_mutex_unlock(&motor.mutex);
    return index < 0 ? index : 0;
}

int MOTORMNG_WaitDone(MOTORMNG_Channel channel, uint32_t timeout_ms)
{
    if (timeout_ms > 3600000U)
        return -EINVAL;
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000U;
    deadline.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    uint64_t generation = motor.generation;
    uint64_t sequence = index >= 0 ? motor.axis[index].sequence : 0;
    while (result == 0 && motor.axis[index].status.state == MOTORMNG_RUNNING) {
        int waited = pthread_cond_timedwait(&motor.changed, &motor.mutex, &deadline);
        if (ready_axis(channel) < 0 || generation != motor.generation)
            result = -ENODEV;
        else if (sequence != motor.axis[index].sequence)
            result = -ECANCELED;
        else if (waited)
            result = -waited;
    }
    if (result == 0) {
        const MOTORMNG_Status *status = &motor.axis[index].status;
        if (status->state == MOTORMNG_FAULT)
            result = status->last_error;
        else if (status->stop_reason == MOTORMNG_STOP_LIMIT)
            result = -ECANCELED;
        else if (status->finite_move && status->stop_reason != MOTORMNG_STOP_TARGET)
            result = -ECANCELED;
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}

int MOTORMNG_NotifyLimits(MOTORMNG_Channel channel, int negative_active, int positive_active)
{
    pthread_mutex_lock(&motor.mutex);
    int index = ready_axis(channel);
    int result = index < 0 ? index : 0;
    if (result == 0) {
        MOTORMNG_Status *status = &motor.axis[index].status;
        status->negative_limit = !!negative_active;
        status->positive_limit = !!positive_active;
        if (status->state == MOTORMNG_RUNNING &&
            ((status->direction == MOTORMNG_FORWARD && positive_active) ||
             (status->direction == MOTORMNG_REVERSE && negative_active)))
            result = stop_axis((unsigned int)index, MOTORMNG_STOP_LIMIT);
    }
    pthread_mutex_unlock(&motor.mutex);
    return result;
}
