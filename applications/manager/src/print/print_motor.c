#include "print_motor.h"

#include <errno.h>

int64_t print_motor_position(const PRINTMNG_AxisConfig *axis, uint32_t distance_um)
{
    uint64_t numerator = (uint64_t)distance_um * MOTORMNG_FULL_STEPS_PER_REV *
                         MOTORMNG_MICROSTEPS_PER_FULL_STEP;
    int64_t steps = (numerator + axis->travel_um_per_motor_rev / 2U) /
                    axis->travel_um_per_motor_rev;
    return axis->forward_direction == MOTORMNG_FORWARD ? steps : -steps;
}

int print_motor_validate(const PRINTMNG_AxisConfig *axis, uint32_t distance_um)
{
    if (!axis->enabled)
        return 0;
    if (!axis->travel_um_per_motor_rev)
        return -ENODATA;
    int64_t steps = print_motor_position(axis, distance_um);
    if (steps < 0)
        steps = -steps;
    if (distance_um && !steps)
        return -ERANGE;
    return steps > MOTORMNG_MAX_MOVE_MICROSTEPS ? -ERANGE : 0;
}

int print_motor_init(const PRINTMNG_Config *config)
{
    if (config->mode == PRINTMNG_DRY_RUN)
        return 0;
    MOTORMNG_Config motors;
    MOTORMNG_DefaultConfig(&motors);
    motors.i2c_bus = config->i2c_bus;
    motors.channel[0].enabled = 0;
    motors.channel[1].enabled = 0;
    const PRINTMNG_AxisConfig *axes[] = {&config->scan_axis, &config->feed_axis};
    for (unsigned int index = 0; index < 2; ++index) {
        const PRINTMNG_AxisConfig *axis = axes[index];
        if (!axis->enabled)
            continue;
        unsigned int channel = axis->channel == MOTORMNG_CH12 ? 0U : 1U;
        motors.channel[channel].enabled = 1;
        motors.channel[channel].target_pps = axis->pps;
        motors.channel[channel].current_estimate_ma = axis->current_estimate_ma;
        motors.channel[channel].travel_um_per_motor_rev = axis->travel_um_per_motor_rev;
    }
    return MOTORMNG_Init(&motors);
}

int print_motor_close(const PRINTMNG_Config *config)
{
    return config->mode == PRINTMNG_DRY_RUN ? 0 : MOTORMNG_DeInit();
}

int print_motor_stop(const PRINTMNG_Config *config)
{
    return config->mode == PRINTMNG_DRY_RUN ? 0 : MOTORMNG_StopAll();
}

int print_motor_reference(const PRINTMNG_AxisConfig *axis)
{
    return axis->enabled ? MOTORMNG_SetPosition(axis->channel, 0) : 0;
}

int print_motor_move(const PRINTMNG_AxisConfig *axis, uint32_t target_um)
{
    return MOTORMNG_MoveTo(axis->channel, print_motor_position(axis, target_um));
}

int print_motor_poll(const PRINTMNG_AxisConfig *axis, int *complete, int64_t *position)
{
    MOTORMNG_Status status;
    int result = MOTORMNG_GetStatus(axis->channel, &status);
    if (result < 0)
        return result;
    if (status.state == MOTORMNG_FAULT)
        return status.last_error < 0 ? status.last_error : -EIO;
    *position = status.command_position_microsteps;
    *complete = status.state != MOTORMNG_RUNNING;
    if (*complete && (status.state != MOTORMNG_DONE || status.stop_reason != MOTORMNG_STOP_TARGET))
        return -ECANCELED;
    return 0;
}
