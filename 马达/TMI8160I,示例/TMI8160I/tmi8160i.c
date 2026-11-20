#include "tmi8160i.h"
#include "i2c_controller.h"

static int i2cFd = -1;
static uint8_t const ChipSlaveAddress = 0x56;

/***************** 寄存器地址定义 *****************/
static uint8_t const CHIP_ID0			 = 0x00; // 芯片ID0，0x81													
static uint8_t const CHIP_ID1			 = 0x01; // 芯片ID1，0x6C													
static uint8_t const GLOBAL_CTRL		 = 0x02; // 全局控制寄存器											
static uint8_t const PA_PB_EXPANSION	 = 0x03; // IO 扩展寄存器													
static uint8_t const GLOBAL_STATE		 = 0x04; // 全局状态寄存器												
static uint8_t const OVP_POT			 = 0x05; // 过压保护寄存器										
static uint8_t const IRCUT_CTRL			 = 0x10; // 通道IR cut 控制寄存器									
static uint8_t const CH12_MODE_CTRL		 = 0x20; // 通道 12 工作模式控制寄存器									
static uint8_t const CH12_STATE			 = 0x21; // 通道 12 状态寄存器										
static uint8_t const CH12_DIRECTION_CTRL = 0x22; // 通道 12 方向控制寄存器											
static uint8_t const CH12_CLOCK_CTRL	 = 0x23; // 通道 12 系统时钟分频控制寄存器								
static uint8_t const CH12_SPEED_CTRL	 = 0x24; // 通道 12 速度分频控制寄存器		
static uint8_t const CH12_AGC_COEF	     = 0x25; // 通道 12 自增益控制寄存器								
static uint8_t const CH12_SPEED_CNT		 = 0x26; // 通道 12 速度计数控制寄存器									
static uint8_t const CH12_PHASE_CNT		 = 0x28; // 通道 12 当前相位计数寄存器									
static uint8_t const CH12_CYCLE_CNT		 = 0x2A; // 通道 12 当前圈数计数寄存器									
static uint8_t const CH12_PHASE_SET		 = 0x2C; // 通道 12 相位设定寄存器										
static uint8_t const CH12_CYCLE_SET		 = 0x2E; // 通道 12 圈数设定寄存器										
static uint8_t const CH1_PULSE_SET		 = 0x30; // 通道 1 PWM 宽度设定寄存器									
static uint8_t const CH2_PULSE_SET		 = 0x32; // 通道 2 PWM 宽度设定寄存器									
static uint8_t const CH34_MODE_CTRL		 = 0x40; // 通道 34 工作模式控制寄存器									
static uint8_t const CH34_STATE			 = 0x41; // 通道 34 状态寄存器										
static uint8_t const CH34_DIRECTION_CTRL = 0x42; // 通道 34 方向控制寄存器											
static uint8_t const CH34_CLOCK_CTRL	 = 0x43; // 通道 34 系统时钟分频控制寄存器								
static uint8_t const CH34_SPEED_CTRL	 = 0x44; // 通道 34 速度分频控制寄存器	
static uint8_t const CH34_AGC_COEF	     = 0x45; // 通道 34 自增益控制寄存器									
static uint8_t const CH34_SPEED_CNT		 = 0x46; // 通道 34 速度计数控制寄存器									
static uint8_t const CH34_PHASE_CNT		 = 0x48; // 通道 34 当前相位计数寄存器									
static uint8_t const CH34_CYCLE_CNT		 = 0x4A; // 通道 34 当前圈数计数寄存器									
static uint8_t const CH34_PHASE_SET		 = 0x4C; // 通道 34 相位设定寄存器										
static uint8_t const CH34_CYCLE_SET		 = 0x4E; // 通道 34 圈数设定寄存器										
static uint8_t const CH3_PULSE_SET		 = 0x50; // 通道 3 PWM 宽度设定寄存器									
static uint8_t const CH4_PULSE_SET		 = 0x52; // 通道 4 PWM 宽度设定寄存器									

/***************** 寄存器地址定义 *****************/

/***************** 寄存器值 *****************/												
static uint8_t GLOBAL_CTRL_VAL		   = 0x80; // 全局控制寄存器											
static uint8_t PA_PB_EXPANSION_VAL   = 0xA0; // IO 扩展寄存器													
static uint8_t GLOBAL_STATE_VAL		   = 0x90; // 全局状态寄存器												
static uint8_t OVP_POT_VAL			   = 0x00; // 过压保护寄存器										
static uint8_t IRCUT_CTRL_VAL		   = 0x00; // 通道IR cut 控制寄存器									
static uint8_t CH12_MODE_CTRL_VAL	   = 0x17; // 通道 12 工作模式控制寄存器									
static uint8_t CH12_STATE_VAL		   = 0x31; // 通道 12 状态寄存器										
static uint8_t CH12_DIRECTION_CTRL_VAL = 0x00; // 通道 12 方向控制寄存器											
static uint8_t CH12_CLOCK_CTRL_VAL	   = 0x07; // 通道 12 系统时钟分频控制寄存器								
static uint8_t CH12_SPEED_CTRL_VAL	   = 0x00; // 通道 12 速度分频控制寄存器
static uint8_t CH12_AGC_COEF_VAL	   = 0x80; // 通道 12 自增益控制寄存器									
static uint16_t CH12_SPEED_CNT_VAL	   = 0x0000; // 通道 12 速度计数控制寄存器									
static uint16_t CH12_PHASE_CNT_VAL	   = 0x0000; // 通道 12 当前相位计数寄存器									
static uint16_t CH12_CYCLE_CNT_VAL	   = 0x0000; // 通道 12 当前圈数计数寄存器									
static uint16_t CH12_PHASE_SET_VAL	   = 0x0000; // 通道 12 相位设定寄存器										
static uint16_t CH12_CYCLE_SET_VAL	   = 0x0000; // 通道 12 圈数设定寄存器										
static uint16_t CH1_PULSE_SET_VAL	   = 0x0000; // 通道 1 PWM 宽度设定寄存器									
static uint16_t CH2_PULSE_SET_VAL	   = 0x0000; // 通道 2 PWM 宽度设定寄存器									
static uint8_t CH34_MODE_CTRL_VAL	   = 0x17; // 通道 34 工作模式控制寄存器									
static uint8_t CH34_STATE_VAL		   = 0x31; // 通道 34 状态寄存器										
static uint8_t CH34_DIRECTION_CTRL_VAL = 0x00; // 通道 34 方向控制寄存器											
static uint8_t CH34_CLOCK_CTRL_VAL	   = 0x07; // 通道 34 系统时钟分频控制寄存器								
static uint8_t CH34_SPEED_CTRL_VAL	   = 0x00; // 通道 34 速度分频控制寄存器	
static uint8_t CH34_AGC_COEF_VAL	   = 0x80; // 通道 34 自增益控制寄存器								
static uint16_t CH34_SPEED_CNT_VAL	   = 0x0000; // 通道 34 速度计数控制寄存器									
static uint16_t CH34_PHASE_CNT_VAL	   = 0x0000; // 通道 34 当前相位计数寄存器									
static uint16_t CH34_CYCLE_CNT_VAL	   = 0x0000; // 通道 34 当前圈数计数寄存器									
static uint16_t CH34_PHASE_SET_VAL	   = 0x0000; // 通道 34 相位设定寄存器										
static uint16_t CH34_CYCLE_SET_VAL	   = 0x0000; // 通道 34 圈数设定寄存器										
static uint16_t CH3_PULSE_SET_VAL	   = 0x0000; // 通道 3 PWM 宽度设定寄存器									
static uint16_t CH4_PULSE_SET_VAL	   = 0x0000; // 通道 4 PWM 宽度设定寄存器
/***************** 寄存器值 *****************/

static int write_byte(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    if (write(i2cFd, buffer, 2) != 2) {
        log_debug("Failed to write byte to I2C device");
        return -1;
    }

    log_debug("[W][Register(0x%02x)]: %02x\n",reg,value);
    return 0;
}

static int read_byte(uint8_t reg, uint8_t *value) {
    if (write(i2cFd, &reg, 1) != 1) {
        log_debug("Failed to set register for read");
        return -1;
    }
    
    if (read(i2cFd, value, 1) != 1) {
        log_debug("Failed to read byte from I2C device");
        return -1;
    }

    log_debug("[R][Register(0x%02x)]: %02x\n",reg,*value);
    return 0;
}

static int write_2byte(uint8_t reg, uint16_t value)
{
    uint8_t buffer[3] = {reg, (uint8_t)value,(uint8_t)(value >> 8)};
    if (write(i2cFd, buffer, 3) != 3) {
        log_debug("Failed to write byte to I2C device");
        return -1;
    }

    log_debug("[W][Register(0x%02x)]: %02x %02x\n",reg,buffer[1],buffer[2]);
    return 0;
}

static int read_2byte(uint8_t reg, uint16_t *value)
{
    if (write(i2cFd, &reg, 1) != 1) {
        log_debug("Failed to set register for read");
        return -1;
    }

    uint8_t buf[2] = {0};
    if (read(i2cFd, buf, 2) != 1) {
        log_debug("Failed to read byte from I2C device");
        return -1;
    }

    *value = (buf[1] << 8) | buf[0];

    log_debug("[R][Register(0x%02x)]: %02x %02x\n",reg,buf[0],buf[1]);
    return 0;
}

static void resetRegisters(){
    GLOBAL_CTRL_VAL		   = 0x80;
    PA_PB_EXPANSION_VAL  = 0xA0;	
    GLOBAL_STATE_VAL	   = 0x90;
    OVP_POT_VAL			   = 0x00;
    IRCUT_CTRL_VAL		   = 0x00; 
    CH12_MODE_CTRL_VAL	   = 0x17; 	
    CH12_STATE_VAL		   = 0x31; 
    CH12_DIRECTION_CTRL_VAL = 0x00; 		
    CH12_CLOCK_CTRL_VAL	   = 0x07; 
    CH12_SPEED_CTRL_VAL	   = 0x00; 
    CH12_AGC_COEF_VAL      = 0x80;	
    CH12_SPEED_CNT_VAL	   = 0x0000; 
    CH12_PHASE_CNT_VAL	   = 0x0000; 
    CH12_CYCLE_CNT_VAL	   = 0x0000; 
    CH12_PHASE_SET_VAL	   = 0x0000; 
    CH12_CYCLE_SET_VAL	   = 0x0000; 
    CH1_PULSE_SET_VAL	   = 0x0000; 
    CH2_PULSE_SET_VAL	   = 0x0000; 
    CH34_MODE_CTRL_VAL	   = 0x17; 
    CH34_STATE_VAL		   = 0x31; 
    CH34_DIRECTION_CTRL_VAL = 0x00; 		
    CH34_CLOCK_CTRL_VAL	   = 0x07; 
    CH34_SPEED_CTRL_VAL	   = 0x00; 	
    CH34_AGC_COEF_VAL	   = 0x80;
    CH34_SPEED_CNT_VAL	   = 0x0000; 
    CH34_PHASE_CNT_VAL	   = 0x0000; 
    CH34_CYCLE_CNT_VAL	   = 0x0000; 
    CH34_PHASE_SET_VAL	   = 0x0000; 
    CH34_CYCLE_SET_VAL	   = 0x0000; 
    CH3_PULSE_SET_VAL	   = 0x0000; 
    CH4_PULSE_SET_VAL	   = 0x0000; 
}


void setI2CHandle(int fd)
{
    i2cFd = fd;
}

void setSlaveAddress(int fd)
{
    return i2c_set_slave_address(fd, ChipSlaveAddress);
}

uint16_t getID()
{
    uint8_t id0,id1;
    return read_byte(CHIP_ID0,&id0) < 0 || read_byte(CHIP_ID1,&id1) < 0 ? 0:(id1 << 8 | id0);
}

int globalEnabel()
{
    uint8_t regData = 0x8A;
    if(write_byte(GLOBAL_CTRL,regData) < 0){
        return -1;
    }
    resetRegisters();
    GLOBAL_CTRL_VAL = regData;
    return 0;
}

int frequencyJitterEnabele(uint8_t enable)
{
    uint8_t regData = (GLOBAL_CTRL_VAL & 0b11101111) | (enable == 0 ? 0 : 1);
    if(write_byte(GLOBAL_CTRL,regData) < 0){
        return -1;
    }

    GLOBAL_CTRL_VAL = regData;
    return 0;
}

int setOvpEnable(Ovp ovp)
{
    uint8_t regData = 0b1000 | ovp;
    if(write_byte(OVP_POT,regData) < 0){
        return -1;
    }

    OVP_POT_VAL = regData;
    return 0;
}

int setOvpDisable()
{
    uint8_t regData = OVP_POT_VAL & 0b0111;
    if(write_byte(OVP_POT,regData) < 0){
        return -1;
    }

    OVP_POT_VAL = regData;
    return 0;
}

int setChannelModeAndEnable(Channel channels,WorkMode mode)
{
    if(CH_CONTAINS(channels, CH12)){
        uint8_t regData = 0b10000000 | mode;
        if(write_byte(CH12_MODE_CTRL,regData) < 0){
            return -1;
        }
        CH12_MODE_CTRL_VAL = regData;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        uint8_t regData = 0b10000000 | mode;
        if(write_byte(CH34_MODE_CTRL,regData) < 0){
            return -1;
        }
        CH34_MODE_CTRL_VAL = regData;
    }
    return 0;
}


int setMicroStep(Channel channels, MicroStep microStep)
{
    if(CH_CONTAINS(channels, CH12)){
        uint8_t regData = (microStep << 4) | (CH12_DIRECTION_CTRL_VAL & 0b00001111);
        if(write_byte(CH12_DIRECTION_CTRL,regData) < 0){
            return -1;
        }
        CH12_DIRECTION_CTRL_VAL = regData;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        uint8_t regData = (microStep << 4) | (CH34_DIRECTION_CTRL_VAL & 0b00001111);
        if(write_byte(CH34_DIRECTION_CTRL,regData) < 0){
            return -1;
        }
        CH34_DIRECTION_CTRL_VAL = regData;
    }
    return 0;
}

int setClockDiv(Channel channels, ClockPreDiv clockPreDiv, ClockDiv clockDiv)
{
    if(CH_CONTAINS(channels, CH12)){
        uint8_t regData = (clockPreDiv << 5) | clockDiv;
        if(write_byte(CH12_CLOCK_CTRL,regData) < 0){
            return -1;
        }
        CH12_CLOCK_CTRL_VAL = regData;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        uint8_t regData = (clockPreDiv << 5) | clockDiv;
        if(write_byte(CH34_CLOCK_CTRL,regData) < 0){
            return -1;
        }
        CH34_CLOCK_CTRL_VAL = regData;
    }

    return 0;
}

int setSpeedDiv(Channel channels, SpeedPreDiv speedPreDiv, SpeedDiv speedDiv)
{
    if(CH_CONTAINS(channels, CH12)){
        uint8_t regData = (speedPreDiv << 5) | speedDiv;
        if(write_byte(CH12_SPEED_CTRL,regData) < 0){
            return -1;
        }
        CH12_SPEED_CTRL_VAL = regData;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        uint8_t regData = (speedPreDiv << 5) | speedDiv;
        if(write_byte(CH34_SPEED_CTRL,regData) < 0){
            return -1;
        }
        CH34_SPEED_CTRL_VAL = regData;
    }

    return 0;
}

int setChannelAction(Channel channels, MotorAction motorAction)
{
    if (CH_CONTAINS(channels, CH12))
    {
        WorkMode workmode = (CH12_MODE_CTRL_VAL & 0b111);
        if(workmode == DIRECT_OUT || workmode == BLDC){
            uint8_t actionFlag = motorAction,actionData = 0;
            if(CH_CONTAINS(channels, CH1)){
                actionData = actionFlag;
            }
            if(CH_CONTAINS(channels, CH2)){
                actionData |= (actionFlag << 2);
            }
            uint8_t regData = (CH12_DIRECTION_CTRL_VAL & 0b11110000) | actionData;
            if(write_byte(CH12_DIRECTION_CTRL, regData) < 0){
                return -1;
            }
            CH12_DIRECTION_CTRL_VAL = regData;
        }else {
            uint8_t actionFlag = motorAction;
            uint8_t regData = (CH12_DIRECTION_CTRL_VAL & 0b11110000) | actionFlag;
            if(write_byte(CH12_DIRECTION_CTRL, regData) < 0){
                return -1;
            }
            CH12_DIRECTION_CTRL_VAL = regData;
        }
    }

    if (CH_CONTAINS(channels, CH34))
    {
        WorkMode workmode = (CH34_MODE_CTRL_VAL & 0b111);
        if(workmode == DIRECT_OUT || workmode == BLDC){
            uint8_t actionFlag = motorAction,actionData = 0;
            if(CH_CONTAINS(channels, CH3)){
                actionData = actionFlag;
            }
            if(CH_CONTAINS(channels, CH4)){
                actionData |= (actionFlag << 2);
            }
            uint8_t regData = (CH34_DIRECTION_CTRL_VAL & 0b11110000) | actionData;
            if(write_byte(CH34_DIRECTION_CTRL, regData) < 0){
                return -1;
            }
            CH34_DIRECTION_CTRL_VAL = regData;
        }else {
            uint8_t actionFlag = motorAction;
            uint8_t regData = (CH34_DIRECTION_CTRL_VAL & 0b11110000) | actionFlag;
            if(write_byte(CH34_DIRECTION_CTRL, regData) < 0){
                return -1;
            }
            CH34_DIRECTION_CTRL_VAL = regData;
        }
    }

    return 0;
}

int setSpeedCount(Channel channels, uint16_t speedCount)
{
    if(CH_CONTAINS(channels, CH12)){
        if(write_2byte(CH12_SPEED_CNT,speedCount) < 0){
            return -1;
        }
        CH12_SPEED_CNT_VAL = speedCount;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        if(write_2byte(CH34_SPEED_CNT,speedCount) < 0){
            return -1;
        }
        CH34_SPEED_CNT_VAL = speedCount;
    }

    return 0;
}

int getSpeedCount(Channel channels, uint16_t *speedCount)
{
    uint16_t speed = 0;
    if(CH_CONTAINS(channels, CH12)){
        if(read_2byte(CH12_SPEED_CNT,&speed) < 0){
            return -1;
        }
        CH12_SPEED_CNT_VAL = speed;
    }else if(CH_CONTAINS(channels, CH34)){
        if(read_2byte(CH34_SPEED_CNT,&speed) < 0){
            return -1;
        }
        CH34_SPEED_CNT_VAL = speed;
    }
    *speedCount = speed;
    return 0;
}

int getPhaseCount(Channel channels, uint16_t *phaseCount)
{
    uint16_t phase = 0;
    if(CH_CONTAINS(channels, CH12)){
        if(read_2byte(CH12_PHASE_CNT,&phase) < 0){
            return -1;
        }
        CH12_PHASE_CNT_VAL = phase;
    }else if(CH_CONTAINS(channels, CH34)){
        if(read_2byte(CH34_PHASE_CNT,&phase) < 0){
            return -1;
        }
        CH34_PHASE_CNT_VAL = phase;
    }

    if(phase > 0x3FF) phase = 0x3FF;
    *phaseCount = phase;
    return 0;
}

int setPhaseCount(Channel channels, uint16_t phaseCount)
{
    if(phaseCount > 0x3FF) phaseCount = 0x3FF;
    if(CH_CONTAINS(channels, CH12)){
        if(write_2byte(CH12_PHASE_CNT,phaseCount) < 0){
            return -1;
        }
        CH12_PHASE_CNT_VAL = phaseCount;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        if(write_2byte(CH34_PHASE_CNT,phaseCount) < 0){
            return -1;
        }
        CH34_PHASE_CNT_VAL = phaseCount;
    }

    return 0;
}

int getCycleCount(Channel channels, uint16_t *cycleCount)
{
    uint16_t cycle = 0;
    if(CH_CONTAINS(channels, CH12)){
        if(read_2byte(CH12_CYCLE_CNT,&cycle) < 0){
            return -1;
        }
        CH12_CYCLE_CNT_VAL = cycle;
    }else if(CH_CONTAINS(channels, CH34)){
        if(read_2byte(CH34_CYCLE_CNT,&cycle) < 0){
            return -1;
        }
        CH34_CYCLE_CNT_VAL = cycle;
    }

    *cycleCount = cycle;
    return 0;
}

int setCycleCount(Channel channels, uint16_t cycleCount)
{
    if(CH_CONTAINS(channels, CH12)){
        if(write_2byte(CH12_CYCLE_CNT,cycleCount) < 0){
            return -1;
        }
        CH12_CYCLE_CNT_VAL = cycleCount;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        if(write_2byte(CH34_CYCLE_CNT,cycleCount) < 0){
            return -1;
        }
        CH34_CYCLE_CNT_VAL = cycleCount;
    }

    return 0;
}

int setPhase(Channel channels, uint16_t phase)
{
    if(phase > 0x3FF) phase = 0x3FF;
    
    if(CH_CONTAINS(channels, CH12)){
        if(write_2byte(CH12_PHASE_SET,phase) < 0){
            return -1;
        }
        CH12_PHASE_SET_VAL = phase;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        if(write_2byte(CH34_PHASE_SET,phase) < 0){
            return -1;
        }
        CH34_PHASE_SET_VAL = phase;
    }
    return 0;
}

int setCycle(Channel channels, uint16_t cycle)
{
    if(CH_CONTAINS(channels, CH12)){
        if(write_2byte(CH12_CYCLE_SET,cycle) < 0){
            return -1;
        }
        CH12_CYCLE_SET_VAL = cycle;
    }
    
    if(CH_CONTAINS(channels, CH34)){
        if(write_2byte(CH34_CYCLE_SET,cycle) < 0){
            return -1;
        }
        CH34_CYCLE_SET_VAL = cycle;
    }
    return 0;
}

int setPWM(Channel channels, uint16_t pwm)
{
    if(pwm > 0x100) pwm = 0x100;

    if(CH_CONTAINS(channels, CH1)){
        if(write_2byte(CH1_PULSE_SET,pwm) < 0){
            return -1;
        }
        CH1_PULSE_SET_VAL = pwm;
    }

    if(CH_CONTAINS(channels, CH2)){
        if(write_2byte(CH2_PULSE_SET,pwm) < 0){
            return -1;
        }
        CH2_PULSE_SET_VAL = pwm;
    }

    if(CH_CONTAINS(channels, CH3)){
        if(write_2byte(CH3_PULSE_SET,pwm) < 0){
            return -1;
        }
        CH3_PULSE_SET_VAL = pwm;
    }

    if(CH_CONTAINS(channels, CH4)){
        if(write_2byte(CH4_PULSE_SET,pwm) < 0){
            return -1;
        }
        CH4_PULSE_SET_VAL = pwm;
    }

    return 0;
}

int setIRCUTAndEnable(MotorAction motorAction)
{
    uint8_t actionFlag = 0;
    switch (motorAction)
    {
        case OPEN_CIRCUIT: actionFlag = 0b0000; break;
        case FOREWARD: actionFlag = 0b0101; break;
        case INVERSION: actionFlag = 0b1010; break;
        case BRAKE: actionFlag = 0b1111; break;
        default:break;
    }
    uint8_t actionData = actionFlag | 0b10000000; // IRCUT通道输出使能
    if(write_byte(IRCUT_CTRL,actionData) < 0){
        return -1;
    }

    IRCUT_CTRL_VAL = actionData;
    return 0;
}

int setIRCUTDisable()
{
    uint8_t actionData = IRCUT_CTRL_VAL & 0b01111111;
    if(write_byte(IRCUT_CTRL,actionData) < 0){
        return -1;
    }

    IRCUT_CTRL_VAL = actionData;
    return 0;
}

int setSoftwareLowPower()
{
    if(write_byte(GLOBAL_CTRL,0b01000000) < 0){
        return -1;
    }
    GLOBAL_CTRL_VAL = 0b01000000;
    return 0;
}

int exitSoftwareLowPower()
{
    if(write_byte(GLOBAL_CTRL,0b10001010) < 0){
        return -1;
    }

    GLOBAL_CTRL_VAL = 0b10001010;
    return 0;
}

int setHardwareLowPower()
{
    if(write_byte(GLOBAL_CTRL,0b10000000) < 0){
        return -1;
    }
    GLOBAL_CTRL_VAL = 0b10000000;
    return 0;
}

