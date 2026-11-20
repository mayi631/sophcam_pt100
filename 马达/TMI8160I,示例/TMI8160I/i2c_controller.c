#include "i2c_controller.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>

static char defaultDevicePath[56] = "/dev/i2c-0";

int i2c_init(const char *device_path) { 
    if (device_path == NULL) {
        device_path = defaultDevicePath;
    }

    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        i2c_log_error("Failed to open I2C bus");
        return -1;
    }
    
    return fd;
}

void i2c_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int i2c_set_slave_address(int fd, uint8_t addr) {
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        i2c_log_error("Failed to set I2C slave address");
        return -1;
    }
    return 0;
}

int i2c_write_data(int fd, uint8_t reg, const uint8_t *data, uint16_t length) {
    uint8_t buffer[length + 1];
    buffer[0] = reg;
    memcpy(buffer + 1, data, length);
    
    if (write(fd, buffer, length + 1) != (length + 1)) {
        i2c_log_info("Failed to write to I2C device");
        return -1;
    }

#if I2C_LOG_LEVEL <= I2C_LOG_LEVEL_DEBUG
    printf("[W][Register(0x%02x)]:",reg);
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
#endif
    
    return length;
}

int i2c_read_data(int fd, uint8_t reg, uint8_t *data, uint16_t length) {
    if (write(fd, &reg, 1) != 1) {
        i2c_log_info("Failed to set register for read");
        return -1;
    }
    
    if (read(fd, data, length) != length) {
        i2c_log_info("Failed to read from I2C device");
        return -1;
    }
    
#if I2C_LOG_LEVEL <= I2C_LOG_LEVEL_DEBUG
    printf("[R][Register(0x%02x)]:",reg);
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
#endif

    return length;
}


