#ifndef MOTORMNG_H
#define MOTORMNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTORMNG_CHANNEL_COUNT 2U
#define MOTORMNG_MICROSTEPS_PER_FULL_STEP 64U
#define MOTORMNG_MICROSTEPS_PER_CYCLE 256U
#define MOTORMNG_FULL_STEPS_PER_REV 20U
#define MOTORMNG_DEFAULT_PPS 510U
#define MOTORMNG_START_PPS 200U
#define MOTORMNG_MAX_PPS 510U
#define MOTORMNG_DEFAULT_CURRENT_ESTIMATE_MA 150U
#define MOTORMNG_MAX_MOVE_MICROSTEPS 16777215U

typedef enum {
    MOTORMNG_CH12 = 12,
    MOTORMNG_CH34 = 34
} MOTORMNG_Channel;

typedef enum {
    MOTORMNG_FORWARD = 0,
    MOTORMNG_REVERSE = 1
} MOTORMNG_Direction;

typedef enum {
    MOTORMNG_IDLE = 0,
    MOTORMNG_RUNNING,
    MOTORMNG_DONE,
    MOTORMNG_FAULT
} MOTORMNG_State;

typedef enum {
    MOTORMNG_STOP_NONE = 0,
    MOTORMNG_STOP_REQUEST,
    MOTORMNG_STOP_TARGET,
    MOTORMNG_STOP_LIMIT,
    MOTORMNG_STOP_FAULT
} MOTORMNG_StopReason;

typedef struct {
    uint32_t enabled;
    uint32_t target_pps;
    uint32_t current_estimate_ma;
    uint32_t travel_um_per_motor_rev;
} MOTORMNG_ChannelConfig;

typedef struct {
    uint32_t i2c_bus;
    MOTORMNG_ChannelConfig channel[MOTORMNG_CHANNEL_COUNT];
} MOTORMNG_Config;

typedef struct {
    MOTORMNG_State state;
    MOTORMNG_StopReason stop_reason;
    MOTORMNG_Direction direction;
    uint32_t target_pps;
    uint32_t command_pps;
    uint32_t speed_count;
    uint32_t current_estimate_ma;
    uint32_t agc;
    uint32_t finite_move;
    uint32_t position_referenced;
    uint32_t negative_limit;
    uint32_t positive_limit;
    int64_t command_position_microsteps;
    int64_t target_position_microsteps;
    int last_error;
} MOTORMNG_Status;

void MOTORMNG_DefaultConfig(MOTORMNG_Config *config);
int MOTORMNG_Init(const MOTORMNG_Config *config);
int MOTORMNG_DeInit(void);
int MOTORMNG_Start(MOTORMNG_Channel channel, MOTORMNG_Direction direction);
int MOTORMNG_Stop(MOTORMNG_Channel channel);
int MOTORMNG_StopAll(void);
int MOTORMNG_SetSpeed(MOTORMNG_Channel channel, uint32_t pps);
int MOTORMNG_SetLinearSpeedUm(MOTORMNG_Channel channel, uint32_t um_per_second);
int MOTORMNG_MoveRelative(MOTORMNG_Channel channel, int64_t microsteps);
int MOTORMNG_MoveTo(MOTORMNG_Channel channel, int64_t position_microsteps);
int MOTORMNG_MoveCyclesPhase(MOTORMNG_Channel channel, uint32_t cycles,
                            uint32_t phase, MOTORMNG_Direction direction);
int MOTORMNG_MoveDistanceUm(MOTORMNG_Channel channel, int32_t distance_um);
int MOTORMNG_SetPosition(MOTORMNG_Channel channel, int64_t position_microsteps);
int MOTORMNG_GetStatus(MOTORMNG_Channel channel, MOTORMNG_Status *status);
int MOTORMNG_WaitDone(MOTORMNG_Channel channel, uint32_t timeout_ms);
int MOTORMNG_NotifyLimits(MOTORMNG_Channel channel, int negative_active, int positive_active);

#ifdef __cplusplus
}
#endif

#endif
