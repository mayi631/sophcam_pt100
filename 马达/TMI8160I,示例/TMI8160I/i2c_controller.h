#ifndef I2C_CONTROLLER_H
#define I2C_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 日志级别定义
typedef enum {
    I2C_LOG_LEVEL_DEBUG = 1,
    I2C_LOG_LEVEL_INFO  = 2,
    I2C_LOG_LEVEL_ERROR = 3
} i2c_log_level_t;

// 默认日志级别
#ifndef I2C_LOG_LEVEL
#define I2C_LOG_LEVEL I2C_LOG_LEVEL_DEBUG
#endif

// 日志宏
#define i2c_log_debug(fmt, ...) \
    do { \
        if (I2C_LOG_LEVEL <= I2C_LOG_LEVEL_DEBUG) { \
            printf("[I2C DEBUG] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define i2c_log_info(fmt, ...) \
    do { \
        if (I2C_LOG_LEVEL <= I2C_LOG_LEVEL_INFO) { \
            printf("[I2C INFO]  %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define i2c_log_error(fmt, ...) \
    do { \
        if (I2C_LOG_LEVEL <= I2C_LOG_LEVEL_ERROR) { \
            printf("[I2C ERROR] %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)


// 初始化I2C总线
// 参数: device_path - I2C总线(如/dev/i2c-1)
// 返回: 文件描述符，失败返回-1
int i2c_init(const char *device_path);

// 关闭I2C设备
// 参数: fd - 由i2c_init返回的文件描述符
void i2c_close(int fd);

// 设置I2C从设备地址
// 参数: fd - 文件描述符
//       addr - 7位I2C设备地址
// 返回: 成功返回0，失败返回-1
int i2c_set_slave_address(int fd, uint8_t addr);

// I2C写操作
// 参数: fd - 文件描述符
//       reg - 寄存器地址
//       data - 要写入的数据
//       length - 数据长度
// 返回: 成功返回写入的字节数，失败返回-1
int i2c_write_data(int fd, uint8_t reg, const uint8_t *data, uint16_t length);

// I2C读操作
// 参数: fd - 文件描述符
//       reg - 寄存器地址
//       data - 存储读取数据的缓冲区
//       length - 要读取的数据长度
// 返回: 成功返回读取的字节数，失败返回-1
int i2c_read_data(int fd, uint8_t reg, uint8_t *data, uint16_t length);



#endif //I2C_CONTROLLER_H

