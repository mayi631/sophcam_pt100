#ifndef PRINTMNG_H
#define PRINTMNG_H

#include <stdint.h>
#include "motormng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRINTMNG_PATH_MAX 512U
#define PRINTMNG_MAX_WAIT_MS 3600000U

typedef enum {
    PRINTMNG_DRY_RUN = 0,
    PRINTMNG_MOTOR_SIMULATION,
    PRINTMNG_REAL_PRINT
} PRINTMNG_Mode;

typedef enum {
    PRINTMNG_IDLE = 0,
    PRINTMNG_QUEUED,
    PRINTMNG_PREPARING,
    PRINTMNG_PREFEED,
    PRINTMNG_HOMING,
    PRINTMNG_SETTLING,
    PRINTMNG_SCANNING,
    PRINTMNG_FEEDING,
    PRINTMNG_EJECTING,
    PRINTMNG_COMPLETED,
    PRINTMNG_CANCELLED,
    PRINTMNG_FAILED
} PRINTMNG_State;

typedef enum {
    PRINTMNG_LEFT_TO_RIGHT = 0,
    PRINTMNG_RIGHT_TO_LEFT
} PRINTMNG_ScanDirection;

typedef enum {
    PRINTMNG_NO_PAPER = 1U << 0,
    PRINTMNG_COVER_OPEN = 1U << 1,
    PRINTMNG_HEAD_OVERHEAT = 1U << 2,
    PRINTMNG_PAPER_JAM = 1U << 3,
    PRINTMNG_SENSOR_INVALID = 1U << 4
} PRINTMNG_SafetyFault;

typedef struct {
    char image_path[PRINTMNG_PATH_MAX];
    uint32_t passes;
    uint32_t scan_width_um;
    uint32_t feed_length_um;
    uint32_t head_width_um;
    uint32_t nozzle_pitch_nm;
    uint32_t prefeed_um;
    uint32_t eject_um;
    uint32_t scan_travel_um;
    uint32_t data_start_um[2];
} PRINTMNG_Job;

typedef struct {
    uint32_t sweeps;
    uint32_t columns_per_sweep;
    uint32_t image_rows;
    uint32_t total_feed_um;
    uint64_t total_columns;
} PRINTMNG_Plan;

typedef struct {
    uint32_t index;
    uint32_t total;
    uint32_t interlace_phase;
    PRINTMNG_ScanDirection direction;
    uint32_t data_start_um;
    uint32_t data_end_um;
    uint32_t columns;
    uint32_t feed_offset_um;
} PRINTMNG_Sweep;

typedef struct {
    uint32_t columns_sent;
    uint32_t complete;
} PRINTMNG_HeadStatus;

typedef struct {
    uint32_t enabled;
    MOTORMNG_Channel channel;
    MOTORMNG_Direction forward_direction;
    uint32_t travel_um_per_motor_rev;
    uint32_t pps;
    uint32_t current_estimate_ma;
} PRINTMNG_AxisConfig;

typedef struct {
    void *context;
    int (*check_safety)(void *context, uint32_t *faults);
    int (*home_begin)(void *context, MOTORMNG_Channel channel, MOTORMNG_Direction toward_left);
    int (*home_poll)(void *context, uint32_t *complete);
    int (*home_end)(void *context);
    int (*prepare_image)(void *context, const PRINTMNG_Job *job, const PRINTMNG_Plan *plan);
    int (*arm_sweep)(void *context, const PRINTMNG_Sweep *sweep);
    int (*poll_sweep)(void *context, PRINTMNG_HeadStatus *status);
    int (*end_sweep)(void *context);
    int (*finish)(void *context);
    int (*abort)(void *context);
} PRINTMNG_Backend;

typedef struct {
    uint64_t job_id;
    PRINTMNG_Mode mode;
    PRINTMNG_State state;
    uint32_t active;
    uint32_t cancel_requested;
    uint32_t scan_motor_enabled;
    uint32_t feed_motor_enabled;
    uint32_t physical_home_verified;
    PRINTMNG_Plan plan;
    PRINTMNG_Sweep sweep;
    uint32_t sweeps_done;
    uint32_t columns_done;
    uint64_t total_columns_done;
    uint32_t feed_commanded_um;
    uint32_t safety_faults;
    int last_error;
    int cleanup_error;
} PRINTMNG_Status;

typedef struct {
    PRINTMNG_Mode mode;
    uint32_t i2c_bus;
    PRINTMNG_AxisConfig scan_axis;
    PRINTMNG_AxisConfig feed_axis;
    uint32_t allow_unhomed_scan;
    uint32_t poll_ms;
    uint32_t settle_ms;
    uint32_t simulated_scan_ms;
    uint32_t simulated_feed_ms;
    uint32_t move_timeout_ms;
    uint32_t home_timeout_ms;
    PRINTMNG_Backend backend;
    void (*on_status)(const PRINTMNG_Status *status, void *context);
    void *status_context;
} PRINTMNG_Config;

void PRINTMNG_DefaultConfig(PRINTMNG_Config *config);
void PRINTMNG_DefaultJob(PRINTMNG_Job *job);
int PRINTMNG_CalculatePlan(const PRINTMNG_Job *job, PRINTMNG_Plan *plan);
int PRINTMNG_Init(const PRINTMNG_Config *config);
int PRINTMNG_DeInit(void);
int PRINTMNG_Submit(const PRINTMNG_Job *job, uint64_t *job_id);
int PRINTMNG_Cancel(uint64_t job_id);
int PRINTMNG_GetStatus(PRINTMNG_Status *status);
int PRINTMNG_Wait(uint64_t job_id, uint32_t timeout_ms, PRINTMNG_Status *status);

#ifdef __cplusplus
}
#endif

#endif
