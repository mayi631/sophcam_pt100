#ifndef PRINT_MOTOR_H
#define PRINT_MOTOR_H

#include "printmng.h"

int print_motor_init(const PRINTMNG_Config *config);
int print_motor_close(const PRINTMNG_Config *config);
int print_motor_stop(const PRINTMNG_Config *config);
int print_motor_reference(const PRINTMNG_AxisConfig *axis);
int print_motor_move(const PRINTMNG_AxisConfig *axis, uint32_t target_um);
int print_motor_poll(const PRINTMNG_AxisConfig *axis, int *complete, int64_t *position);
int print_motor_validate(const PRINTMNG_AxisConfig *axis, uint32_t distance_um);
int64_t print_motor_position(const PRINTMNG_AxisConfig *axis, uint32_t distance_um);

#endif
