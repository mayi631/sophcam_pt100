#ifndef TMI8160I_H
#define TMI8160I_H

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_ERROR = 3
} log_level_t;

#ifndef API_LOG_LEVEL
#define API_LOG_LEVEL LOG_LEVEL_DEBUG
#endif


#define log_debug(fmt, ...) \
    do { \
        if (API_LOG_LEVEL <= LOG_LEVEL_DEBUG) { \
            printf("[API DEBUG] %s_%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define log_info(fmt, ...) \
    do { \
        if (API_LOG_LEVEL <= LOG_LEVEL_INFO) { \
            printf("[API INFO]  %s_%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define log_error(fmt, ...) \
    do { \
        if (API_LOG_LEVEL <= LOG_LEVEL_ERROR) { \
            printf("[API ERROR] %s_%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)


// 定义通道标志位（最多支持4个通道）
typedef uint8_t Channel;

#define CH_NONE   ((Channel)0)
#define CH1       ((Channel)1 << 0)
#define CH2       ((Channel)1 << 1)
#define CH3       ((Channel)1 << 2)
#define CH4       ((Channel)1 << 3)
#define CH12      ((Channel)0b0011)
#define CH34      ((Channel)0b1100)

// 检查通道是否在组合中
#define CH_CONTAINS(combined, ch) ((combined) & (ch))

#define IS_VALID_CHANNEL(ch)  (((ch) != CH_NONE) && (((ch) & ~0b1111) == 0))

// 通道的细分数
typedef enum {
    MSTEP_DIVIDE_64 = 0b000,      // 1/64细分
    MSTEP_DIVIDE_128 = 0b001,     // 1/128细分
    MSTEP_DIVIDE_256 = 0b010, // 1/256细分
} MicroStep;


// 电机的工作模式
typedef enum {
    DIRECT_OUT = 0b000, //输出直接控制模式
    BLDC = 0b001, //直流有刷驱动
    PHASE_1_2  = 0b010, //1-2相励磁模式
    PHASE_2_2  = 0b011, //2-2相励磁模式
    ABSOLUTE_POSITION = 0b101, //绝对位置
    AUTO  = 0b110, //自动控制模式
    RELATIVE_POSITION = 0b111 //相对位置模式
} WorkMode;

// 电机的运动控制(除SPI脉宽控制模式外的控制方式)
typedef enum {
    OPEN_CIRCUIT,   // 输出开路
    FOREWARD,       // 正转
    INVERSION,      // 反转
    BRAKE,          // 刹车
} MotorAction;

typedef enum {
    CLOCK_PRE_DIVIDE_1 = 0b000, // 不分频
    CLOCK_PRE_DIVIDE_2 = 0b001, // 2分频
    CLOCK_PRE_DIVIDE_4 = 0b010,
    CLOCK_PRE_DIVIDE_8 = 0b011,
    CLOCK_PRE_DIVIDE_16 = 0b100,
    CLOCK_PRE_DIVIDE_32 = 0b101,
    CLOCK_PRE_DIVIDE_64 = 0b110,
    CLOCK_PRE_DIVIDE_128 = 0b111,
} ClockPreDiv;

typedef enum {
    CLOCK_DIVIDE_1   = 0b00000,  // 1 分频 (不分频)
    CLOCK_DIVIDE_2   = 0b00001,  // 2 分频
    CLOCK_DIVIDE_3   = 0b00010,  // 3 分频
    CLOCK_DIVIDE_4   = 0b00011,  // 4 分频
    CLOCK_DIVIDE_5   = 0b00100,  // 5 分频
    CLOCK_DIVIDE_6   = 0b00101,  // 6 分频
    CLOCK_DIVIDE_7   = 0b00110,  // 7 分频
    CLOCK_DIVIDE_8   = 0b00111,  // 8 分频
    CLOCK_DIVIDE_9   = 0b01000,  // 9 分频
    CLOCK_DIVIDE_10  = 0b01001,  // 10 分频
    CLOCK_DIVIDE_11  = 0b01010,  // 11 分频
    CLOCK_DIVIDE_12  = 0b01011,  // 12 分频
    CLOCK_DIVIDE_13  = 0b01100,  // 13 分频
    CLOCK_DIVIDE_14  = 0b01101,  // 14 分频
    CLOCK_DIVIDE_15  = 0b01110,  // 15 分频
    CLOCK_DIVIDE_16  = 0b01111,  // 16 分频
    CLOCK_DIVIDE_17  = 0b10000,  // 17 分频
    CLOCK_DIVIDE_18  = 0b10001,  // 18 分频
    CLOCK_DIVIDE_19  = 0b10010,  // 19 分频
    CLOCK_DIVIDE_20  = 0b10011,  // 20 分频
    CLOCK_DIVIDE_21  = 0b10100,  // 21 分频
    CLOCK_DIVIDE_22  = 0b10101,  // 22 分频
    CLOCK_DIVIDE_23  = 0b10110,  // 23 分频
    CLOCK_DIVIDE_24  = 0b10111,  // 24 分频
    CLOCK_DIVIDE_25  = 0b11000,  // 25 分频
    CLOCK_DIVIDE_26  = 0b11001,  // 26 分频
    CLOCK_DIVIDE_27  = 0b11010,  // 27 分频
    CLOCK_DIVIDE_28  = 0b11011,  // 28 分频
    CLOCK_DIVIDE_29  = 0b11100,  // 29 分频
    CLOCK_DIVIDE_30  = 0b11101,  // 30 分频
    CLOCK_DIVIDE_31  = 0b11110,  // 31 分频
    CLOCK_DIVIDE_32  = 0b11111   // 32 分频
} ClockDiv;

typedef enum {
    SPEED_PRE_DIVIDE_1 = 0b000, // 不分频
    SPEED_PRE_DIVIDE_2 = 0b001, // 2分频
    SPEED_PRE_DIVIDE_4 = 0b010,
    SPEED_PRE_DIVIDE_8 = 0b011,
    SPEED_PRE_DIVIDE_16 = 0b100,
    SPEED_PRE_DIVIDE_32 = 0b101,
    SPEED_PRE_DIVIDE_64 = 0b110,
    SPEED_PRE_DIVIDE_128 = 0b111,
} SpeedPreDiv;

typedef enum {
    SPEED_DIVIDE_1   = 0b00000,  // 1 分频 (不分频)
    SPEED_DIVIDE_2   = 0b00001,  // 2 分频
    SPEED_DIVIDE_3   = 0b00010,  // 3 分频
    SPEED_DIVIDE_4   = 0b00011,  // 4 分频
    SPEED_DIVIDE_5   = 0b00100,  // 5 分频
    SPEED_DIVIDE_6   = 0b00101,  // 6 分频
    SPEED_DIVIDE_7   = 0b00110,  // 7 分频
    SPEED_DIVIDE_8   = 0b00111,  // 8 分频
    SPEED_DIVIDE_9   = 0b01000,  // 9 分频
    SPEED_DIVIDE_10  = 0b01001,  // 10 分频
    SPEED_DIVIDE_11  = 0b01010,  // 11 分频
    SPEED_DIVIDE_12  = 0b01011,  // 12 分频
    SPEED_DIVIDE_13  = 0b01100,  // 13 分频
    SPEED_DIVIDE_14  = 0b01101,  // 14 分频
    SPEED_DIVIDE_15  = 0b01110,  // 15 分频
    SPEED_DIVIDE_16  = 0b01111,  // 16 分频
    SPEED_DIVIDE_17  = 0b10000,  // 17 分频
    SPEED_DIVIDE_18  = 0b10001,  // 18 分频
    SPEED_DIVIDE_19  = 0b10010,  // 19 分频
    SPEED_DIVIDE_20  = 0b10011,  // 20 分频
    SPEED_DIVIDE_21  = 0b10100,  // 21 分频
    SPEED_DIVIDE_22  = 0b10101,  // 22 分频
    SPEED_DIVIDE_23  = 0b10110,  // 23 分频
    SPEED_DIVIDE_24  = 0b10111,  // 24 分频
    SPEED_DIVIDE_25  = 0b11000,  // 25 分频
    SPEED_DIVIDE_26  = 0b11001,  // 26 分频
    SPEED_DIVIDE_27  = 0b11010,  // 27 分频
    SPEED_DIVIDE_28  = 0b11011,  // 28 分频
    SPEED_DIVIDE_29  = 0b11100,  // 29 分频
    SPEED_DIVIDE_30  = 0b11101,  // 30 分频
    SPEED_DIVIDE_31  = 0b11110,  // 31 分频
    SPEED_DIVIDE_32  = 0b11111   // 32 分频
} SpeedDiv;

typedef enum {
    OVP_6 = 0b00, // 6V
    OVP_6_5 = 0b01, // 6.5V
    OVP_7 = 0b10,   //7V
    OVP_7_5 = 0b11, //7.5V
} Ovp;

void setI2CHandle(int fd);

void setSlaveAddress(int fd);

/**
 * @brief 获得芯片的ID，ID0在低位，ID1在高位
 * @return 成功返回0x5081，失败返回0
 */
uint16_t getID();

/**
 * @brief 全局使能
 * @return 成功返回0，失败返回-1
 */
int globalEnabel();

/**
 * @brief 抖频使能或关闭
 * @param enable 0代表关闭，非0则开启
 * @return 成功返回0，失败返回-1
 * @warning 在全局使能之后执行
 */
int frequencyJitterEnabele(uint8_t enable);

/**
 * @brief 设置OVP阈值保护使能
 * @param ovp 阈值
 * @return 成功返回0，失败返回-1
 */
int setOvpEnable(Ovp ovp);

/**
 * @brief 设置OVP阈值保护不使能
 * @param ovp 阈值
 * @return 成功返回0，失败返回-1
 */
int setOvpDisable();

/**
 * @brief 设置芯片的工作模式并使能通道
 * @param channels 需要设置的通道号
 * @param mode 通道的工作模式
 * @return 成功返回0，失败返回-1
 * @warning 让通道输出还需使能通道
 */
int setChannelModeAndEnable(Channel channels,WorkMode mode);


/**
 * @brief 设置指定通道的细分数
 * @param channels 需要设置的通道号
 * @param microStep 需要设置通道的细分数
 * @return 成功返回0，失败返回-1
 * @note 相同系统分频下，细分数越大速度越慢
 */
int setMicroStep(Channel channels,MicroStep microStep);

/**
 * @brief 设置指定通道的时钟分频
 * @param channels 需要设置的通道号
 * @param clockPreDiv 时钟分频
 * @param clockDiv 时钟分频计数
 * @return 成功返回0，失败返回-1
 * @note 相同细分下，系统分频数越大速度越慢,如CH12计算方式，
 * 通道12输出时钟预分频寄存器：CH12_CLK = 28MHz/(2^CH12_CLOCK_PREDIV)，
 * 通道12输出PWM分频寄存器：CH12_PWM = (CH12_CLK/256/(CH12_CLOCK_DIV + 1))
 */
int setClockDiv(Channel channels,ClockPreDiv clockPreDiv,ClockDiv clockDiv);

/**
 * @brief 设置指定通道的速度分频
 * @param channels 需要设置的通道号 CH12 或 CH34
 * @param speedPreDiv 速度分频控制位
 * @param speedDiv 速度分频计数控制位
 * @return 成功返回0，失败返回-1
 * @note 相同细分下，系统分频数越大速度越慢,如CH12计算方式，
 * 通道 从系统时钟分频后速度的分频值：CH12_SPEED_PRE = 28MHz/(2^CH12_SPEED_PREDIV)，
 * 通道 一个步进的速度分频值：CH12_SPEED_DIV_STEP = CH12_SPEED_PRE/(CH12_SPEED_DIV + 1)
 */
int setSpeedDiv(Channel channels,SpeedPreDiv speedPreDiv,SpeedDiv speedDiv);

/**
 * @brief 设置指定通道的运动控制,支持同时设置多个通道
 * @param channels 需要设置的通道号
 * @param motorAction 需要设置通道的转动控制
 * @return 成功返回0，失败返回-1
 * @warning 此函数不适用于SPI脉宽控制模式！调用前需确认当前控制模式;设置的圈数的正反转不一致也会失败
 * @note 多通道设置时，所有通道将采用相同的转动控制参数。
 */
int setChannelAction(Channel channels,MotorAction motorAction);

/**
 * @brief 设置CH12或CH34通道的速度计数
 * @param channels CH12 或 CH34
 * @param speedCount 速度计数
 * @return 成功返回0，失败返回-1
 */
int setSpeedCount(Channel channels,uint16_t speedCount);

/**
 * @brief 获取CH12或CH34通道的速度计数
 * @param channels CH12 或 CH34
 * @param speedCount 速度计数
 * @return 成功返回0，失败返回-1
 */
int getSpeedCount(Channel channels,uint16_t *speedCount);

/**
 * @brief 获取CH12或CH34通道的相位计数
 * @param channels CH12 或 CH34
 * @param phaseCount 获取的相位计数
 * @return 成功返回0，失败返回-1
 * @warning 不能同时获取CH12 和 CH34 的相位
 */
int getPhaseCount(Channel channels,uint16_t *phaseCount);

/**
 * @brief 设置CH12或CH34通道的相位计数
 * @param channels CH12 或 CH34
 * @param phaseCount 相位计数
 * @return 成功返回0，失败返回-1
 * @warning 不能同时设置CH12 和 CH34 的相位
 */
int setPhaseCount(Channel channels,uint16_t phaseCount);

/**
 * @brief 获取指定CH12或CH34通道的圈数计数
 * @param channels CH12 或 CH34
 * @param cycleCount 获取的圈数计数
 * @return 成功返回0，失败返回-1
 * @warning 不能同时获取CH12 和 CH34 的圈数
 */
int getCycleCount(Channel channels,uint16_t *cycleCount);

/**
 * @brief 设置指定CH12或CH34通道的圈数计数
 * @param channels CH12 或 CH34
 * @param cycleCount 圈数计数
 * @return 成功返回0，失败返回-1
 * @warning 不能同时设置CH12 和 CH34 的圈数
 */
int setCycleCount(Channel channels,uint16_t cycleCount);


/**
 * @brief 设置指定CH12或CH34通道的待运动相位
 * @param channels CH12 或 CH34
 * @param phase 待运动的相位
 * @return 成功返回0，失败返回-1
 * @note 最大相位为:10bit，即最大值为:0x3FF
 */
int setPhase(Channel channels,uint16_t phase);

/**
 * @brief 设置指定CH12或CH34通道的待运动圈数
 * @param channels CH12 或 CH34
 * @param phase 待运动的圈数
 * @return 成功返回0，失败返回-1
 * @note 最大圈数为:10bit，即最大值为:0x3FF
 */
int setCycle(Channel channels,uint16_t cycle);

/**
 * @brief 设置指定通道的PWM占空比
 * @param channels CH1 | CH2 | CH3 | CH4，可同时设置多个通道
 * @param pwm 占空比对应的寄存器值(0~256,对应0～100%)
 * @return 成功返回0，失败返回-1
 * @note 最大圈数为:9bit，最大值为:0x100,对应0～100%的占空比
 */
int setPWM(Channel channels,uint16_t pwm);

/**
 * @brief 设置IR电机运动控制方式
 * @param motorAction 控制方式
 * @return 成功返回0，失败返回-1
 */
int setIRCUTAndEnable(MotorAction motorAction);

/**
 * @brief 设置IR电机通道输出禁止
 * @return 成功返回0，失败返回-1
 */
int setIRCUTDisable();

/**
 * @brief 芯片通过软件设置进入低功耗模式
 * @return 成功返回0，失败返回-1
 */
int setSoftwareLowPower();

/**
 * @brief 芯片通过软件设置退出低功耗模式
 * @return 成功返回0，失败返回-1
 */
int exitSoftwareLowPower();

/**
 * @brief 设置芯片通过外部引脚控制低功耗模式的进入和退出
 * @return 成功返回0，失败返回-1
 */
int setHardwareLowPower();

#ifdef __cplusplus
}
#endif

#endif /* TMI8160I_H */