#include "tmi8160i_helper.h"


int initIRCUTAction(MotorAction motorAction)
{
    if(globalEnabel() < 0 ||
    setIRCUTAndEnable(motorAction) < 0){
        return -1;
    }
    return 0;
}

int initDirectControl(Channel channels)
{
    if(globalEnabel() < 0 ||
    setChannelModeAndEnable(channels,DIRECT_OUT) < 0){
        return -1;
    }

    return 0;
}

int initBldcControl(Channel channels, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv,uint16_t pwm)
{
    if(globalEnabel() < 0 ||
    setChannelModeAndEnable(channels,BLDC) < 0 ||
    setClockDiv(channels,CLOCK_PRE_DIVIDE_128,CLOCK_DIVIDE_6) < 0 ||
    setSpeedDiv(channels,speedPreDiv,speedDiv) < 0 || 
    setPWM(channels,pwm) < 0){
        return -1;
    }

    return 0;
}

int init1_2PhaseControl(Channel channels, ClockPreDiv clockPreDiv, ClockDiv clockDiv, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv, uint16_t speedCount, uint16_t phase, uint16_t cycle)
{
    return globalEnabel() == 0 &&
     setChannelModeAndEnable(channels,PHASE_1_2) == 0 &&
     setClockDiv(channels,clockPreDiv,clockDiv) == 0 &&
     setSpeedDiv(channels,speedPreDiv,speedDiv) == 0 &&
     setSpeedCount(channels,speedCount) == 0 && 
     setPhaseCount(channels,0) == 0 &&
     setCycleCount(channels,0) == 0 &&
     setPhase(channels,phase) == 0 &&
     setCycle(channels,cycle) == 0;
}

int init2_2PhaseControl(Channel channels, ClockPreDiv clockPreDiv, ClockDiv clockDiv, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv, uint16_t speedCount, uint16_t phase, uint16_t cycle)
{
    return globalEnabel() == 0 &&
     setChannelModeAndEnable(channels,PHASE_2_2) == 0 &&
     setClockDiv(channels,clockPreDiv,clockDiv) == 0 &&
     setSpeedDiv(channels,speedPreDiv,speedDiv) == 0 &&
     setSpeedCount(channels,speedCount) == 0 && 
     setPhaseCount(channels,0) == 0 &&
     setCycleCount(channels,0) == 0 &&
     setPhase(channels,phase) == 0 &&
     setCycle(channels,cycle) == 0;
}

int initAbsolutePositionControl(Channel channels, MicroStep microStep, ClockPreDiv clockPreDiv, ClockDiv clockDiv, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv, uint16_t speedCount, uint16_t phase, uint16_t cycle)
{
    return globalEnabel() == 0 &&
        setChannelModeAndEnable(channels,ABSOLUTE_POSITION) == 0 &&
        setMicroStep(channels,microStep) == 0 &&
        setClockDiv(channels,clockPreDiv,clockDiv) == 0 &&
        setSpeedDiv(channels,speedPreDiv,speedDiv) == 0 &&
        setSpeedCount(channels,speedCount) == 0 && 
        setPhaseCount(channels,0) == 0 &&
        setCycleCount(channels,0) == 0 &&
        setPhase(channels,phase) == 0 &&
        setCycle(channels,cycle) == 0;
}

int initAutoControl(Channel channels, MicroStep microStep, ClockPreDiv clockPreDiv, ClockDiv clockDiv, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv, uint16_t speedCount)
{
    return globalEnabel() == 0 &&
        setChannelModeAndEnable(channels,AUTO) == 0 &&
        setMicroStep(channels,microStep) == 0 &&
        setClockDiv(channels,clockPreDiv,clockDiv) == 0 &&
        setSpeedDiv(channels,speedPreDiv,speedDiv) == 0 &&
        setSpeedCount(channels,speedCount) == 0;
}

int initRelativePositionControl(Channel channels, MicroStep microStep, ClockPreDiv clockPreDiv, ClockDiv clockDiv, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv, uint16_t speedCount, uint16_t phase, uint16_t cycle)
{
    return globalEnabel() == 0 &&
        setChannelModeAndEnable(channels,RELATIVE_POSITION) == 0 &&
        setMicroStep(channels,microStep) == 0 &&
        setClockDiv(channels,clockPreDiv,clockDiv) == 0 &&
        setSpeedDiv(channels,speedPreDiv,speedDiv) == 0 &&
        setSpeedCount(channels,speedCount) == 0 && 
        setPhaseCount(channels,0) == 0 &&
        setCycleCount(channels,0) == 0 &&
        setPhase(channels,phase) == 0 &&
        setCycle(channels,cycle) == 0;
}
