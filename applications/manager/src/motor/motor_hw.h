#ifndef MOTOR_HW_H
#define MOTOR_HW_H

#include <stdint.h>

struct motor_hw {
    int i2c;
    int memory;
    int lock;
    volatile uint32_t *gpio;
    volatile uint32_t *pinmux;
    uint32_t original_enable;
    uint8_t original_global;
    unsigned int enable_saved;
    unsigned int global_saved;
    unsigned int touched;
    unsigned int faulted;
};

int motor_hw_open(struct motor_hw *hardware, unsigned int bus);
int motor_hw_close(struct motor_hw *hardware);
int motor_hw_begin(struct motor_hw *hardware, unsigned int index, unsigned int reverse,
                   unsigned int count, unsigned int agc, uint32_t microsteps);
int motor_hw_set_speed(struct motor_hw *hardware, unsigned int index, unsigned int count);
int motor_hw_sample(struct motor_hw *hardware, unsigned int index, uint32_t *position, int *ended);
int motor_hw_refresh(struct motor_hw *hardware, unsigned int index);
int motor_hw_stop(struct motor_hw *hardware, unsigned int index);
void motor_hw_disable(struct motor_hw *hardware);

#endif
