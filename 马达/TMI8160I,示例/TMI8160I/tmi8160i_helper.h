#ifndef TMI8160I_HELPER_H
#define TMI8160I_HELPER_H

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "tmi8160i.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 初始化IRCUT控制并直接使能运动
 * @param motorAction 电机输出控制
 * @return 成功返回0，失败返回-1
 */
int initIRCUTAction(MotorAction motorAction);


/**
 * @brief 把通道初始化为直接控制模式
 * @param channels CH1 | CH2 | CH3 | CH4
 * @return 成功返回0，失败返回-1
 */
int initDirectControl(Channel channels);

/**
 * @brief 把通道初始化为直流有刷控制模式
 * @param channels CH1 | CH2 | CH3 | CH4
 * @param speedPreDiv 速度分频控制位
 * @param speedDiv 速度分频计数控制位
 * @param pwm pwm的占空比
 * @return 成功返回0，失败返回-1
 */
int initBldcControl(Channel channels,SpeedPreDiv speedPreDiv,SpeedDiv speedDiv,uint16_t pwm);

/** @brief 把通道初始化为1-2相励磁模式
 *  @param channels CH12 | CH34
 *  @param clockPreDiv 系统时钟分频控制位
 *  @param clockDiv 系统时钟分频计数控制位
 *  @param speedPreDiv 速度分频控制位
 *  @param speedDiv 速度分频计数控制位
 *  @param speedCount 速度计数控制寄存器
 *  @param phase 设定的运动相位
 *  @param cycle 设定的运动圈数
 *  @return 成功返回0，失败返回-1
 */
int init1_2PhaseControl(Channel channels,
    ClockPreDiv clockPreDiv,
    ClockDiv clockDiv,
    SpeedPreDiv speedPreDiv,
    SpeedDiv speedDiv,
    uint16_t speedCount,
    uint16_t phase,
    uint16_t cycle);

/** @brief 把通道初始化为2-2相励磁模式
 *  @param channels CH12 | CH34
 *  @param clockPreDiv 系统时钟分频控制位
 *  @param clockDiv 系统时钟分频计数控制位
 *  @param speedPreDiv 速度分频控制位
 *  @param speedDiv 速度分频计数控制位
 *  @param speedCount 速度计数控制寄存器
 *  @param phase 设定的运动相位
 *  @param cycle 设定的运动圈数
 *  @return 成功返回0，失败返回-1
 */
int init2_2PhaseControl(Channel channels,
    ClockPreDiv clockPreDiv,
    ClockDiv clockDiv,
    SpeedPreDiv speedPreDiv,
    SpeedDiv speedDiv,
    uint16_t speedCount,
    uint16_t phase,
    uint16_t cycle);

/** @brief 把通道初始化为绝对位置模式
 *  @param channels CH12 | CH34
 *  @param clockPreDiv 系统时钟分频控制位
 *  @param clockDiv 系统时钟分频计数控制位
 *  @param speedPreDiv 速度分频控制位
 *  @param speedDiv 速度分频计数控制位
 *  @param speedCount 速度计数控制寄存器
 *  @param phase 设定的运动相位
 *  @param cycle 设定的运动圈数
 *  @return 成功返回0，失败返回-1
 */
int initAbsolutePositionControl(Channel channels,
    MicroStep microStep,
    ClockPreDiv clockPreDiv,
    ClockDiv clockDiv,
    SpeedPreDiv speedPreDiv,
    SpeedDiv speedDiv,
    uint16_t speedCount,
    uint16_t phase,
    uint16_t cycle);

/**
 * @brief 把通道初始化为自动控制模式
 * @param channels CH12 | CH34
 * @param clockPreDiv 时钟预分频寄存器
 * @param clockDiv PWM分频寄存器
 * @param microStep 通道细分数
 * @return 成功返回0，失败返回-1
 */
int initAutoControl(Channel channels,
    MicroStep microStep,
    ClockPreDiv clockPreDiv,
    ClockDiv clockDiv,
    SpeedPreDiv speedPreDiv,
    SpeedDiv speedDiv,
    uint16_t speedCount);

/** @brief 把通道初始化为相对位置模式
 *  @param channels CH12 | CH34
 *  @param microStep CH12 | CH34
 *  @param clockPreDiv 系统时钟分频控制位
 *  @param clockDiv 系统时钟分频计数控制位
 *  @param speedPreDiv 速度分频控制位
 *  @param speedDiv 速度分频计数控制位
 *  @param speedCount 速度计数控制寄存器
 *  @param phase 设定的运动相位
 *  @param cycle 设定的运动圈数
 *  @return 成功返回0，失败返回-1
 */
int initRelativePositionControl(Channel channels,
    MicroStep microStep,
    ClockPreDiv clockPreDiv,
    ClockDiv clockDiv,
    SpeedPreDiv speedPreDiv,
    SpeedDiv speedDiv,
    uint16_t speedCount,
    uint16_t phase,
    uint16_t cycle);

#ifdef __cplusplus
}
#endif

#endif /* TMI8160I_HELPER_H */