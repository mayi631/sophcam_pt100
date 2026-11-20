#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define _FILE_OFFSET_BITS 64

#include "motor_hw.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MOTOR_PAGE_BYTES 4096U
#define MOTOR_GPIO_BASE 0x05021000U
#define MOTOR_PINMUX_BASE 0x03001000U
#define MOTOR_ENABLE (1U << 4)

static unsigned int channel_base(unsigned int index)
{
    return index ? 0x40U : 0x20U;
}

static int register_write(struct motor_hw *hardware, unsigned int address,
                          unsigned int value, unsigned int width)
{
    uint8_t bytes[3] = {address, value, value >> 8};
    ssize_t length = write(hardware->i2c, bytes, width + 1U);
    return length == (ssize_t)(width + 1U) ? 0 : length < 0 ? -errno : -EIO;
}

static int register_read(struct motor_hw *hardware, unsigned int address, uint8_t *value)
{
    uint8_t selected = address;
    for (unsigned int attempt = 0; attempt < 5; ++attempt) {
        errno = 0;
        if (write(hardware->i2c, &selected, 1) == 1 && read(hardware->i2c, value, 1) == 1)
            return 0;
        int failure = errno ? errno : EIO;
        if ((failure != EINTR && failure != EAGAIN) || attempt == 4)
            return -failure;
        struct timespec delay = {0, 2000000L};
        while (nanosleep(&delay, &delay) < 0) {
            if (errno != EINTR)
                return -errno;
        }
    }
    return -EIO;
}

static int register_checked(struct motor_hw *hardware, unsigned int address,
                            unsigned int value, unsigned int width)
{
    int result = register_write(hardware, address, value, width);
    for (unsigned int offset = 0; result == 0 && offset < width; ++offset) {
        uint8_t actual;
        result = register_read(hardware, address + offset, &actual);
        if (result == 0 && actual != ((value >> (offset * 8U)) & 0xffU))
            result = -EIO;
    }
    return result;
}

static int register_word(struct motor_hw *hardware, unsigned int address, unsigned int *value)
{
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        uint8_t before, low, after;
        int result = register_read(hardware, address + 1, &before);
        if (result == 0)
            result = register_read(hardware, address, &low);
        if (result == 0)
            result = register_read(hardware, address + 1, &after);
        if (result < 0)
            return result;
        if (before == after) {
            *value = low | ((unsigned int)after << 8);
            return 0;
        }
    }
    return -EIO;
}

void motor_hw_disable(struct motor_hw *hardware)
{
    hardware->faulted = 1;
    if (hardware->gpio && hardware->gpio != MAP_FAILED)
        hardware->gpio[0] &= ~MOTOR_ENABLE;
}

int motor_hw_stop(struct motor_hw *hardware, unsigned int index)
{
    unsigned int base = channel_base(index);
    int result = register_checked(hardware, base + 2, 0x80, 1);
    int disabled = register_checked(hardware, base, 0x07, 1);
    if (result == 0)
        result = disabled;
    if (result < 0)
        motor_hw_disable(hardware);
    return result;
}

int motor_hw_close(struct motor_hw *hardware)
{
    int result = 0;
    for (unsigned int index = 0; index < 2; ++index) {
        if (hardware->touched & (1U << index)) {
            int stopped = motor_hw_stop(hardware, index);
            if (stopped < 0)
                result = stopped;
        }
    }
    if (hardware->global_saved && !hardware->faulted) {
        int restored = register_checked(hardware, 0x02, hardware->original_global, 1);
        if (restored < 0) {
            result = restored;
            motor_hw_disable(hardware);
        }
    }
    if (hardware->enable_saved && !hardware->faulted)
        hardware->gpio[0] = (hardware->gpio[0] & ~MOTOR_ENABLE) | hardware->original_enable;
    if (hardware->gpio && hardware->gpio != MAP_FAILED)
        munmap((void *)hardware->gpio, MOTOR_PAGE_BYTES);
    if (hardware->pinmux && hardware->pinmux != MAP_FAILED)
        munmap((void *)hardware->pinmux, MOTOR_PAGE_BYTES);
    if (hardware->i2c >= 0)
        close(hardware->i2c);
    if (hardware->memory >= 0)
        close(hardware->memory);
    if (hardware->lock >= 0)
        close(hardware->lock);
    *hardware = (struct motor_hw){.i2c = -1, .memory = -1, .lock = -1};
    return result;
}

int motor_hw_open(struct motor_hw *hardware, unsigned int bus)
{
    *hardware = (struct motor_hw){.i2c = -1, .memory = -1, .lock = -1};
    hardware->lock = open("/tmp/pt100-motor.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    int result = 0;
    if (hardware->lock < 0)
        result = -errno;
    else if (flock(hardware->lock, LOCK_EX | LOCK_NB) < 0)
        result = errno == EWOULDBLOCK ? -EBUSY : -errno;
    if (result == 0) {
        hardware->memory = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
        if (hardware->memory < 0)
            result = -errno;
    }
    if (result == 0) {
        hardware->gpio = mmap(NULL, MOTOR_PAGE_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                              hardware->memory, MOTOR_GPIO_BASE);
        hardware->pinmux = mmap(NULL, MOTOR_PAGE_BYTES, PROT_READ, MAP_SHARED,
                                hardware->memory, MOTOR_PINMUX_BASE);
        if (hardware->gpio == MAP_FAILED || hardware->pinmux == MAP_FAILED)
            result = -errno;
    }
    if (result == 0 && ((hardware->pinmux[0x88 / 4] & 7U) != 3U ||
                       !(hardware->gpio[1] & MOTOR_ENABLE)))
        result = -ENODEV;
    if (result == 0) {
        char path[40];
        snprintf(path, sizeof(path), "/dev/i2c-%u", bus);
        hardware->i2c = open(path, O_RDWR | O_CLOEXEC);
        if (hardware->i2c < 0 || ioctl(hardware->i2c, I2C_SLAVE, 0x56) < 0)
            result = -errno;
    }
    if (result == 0) {
        hardware->original_enable = hardware->gpio[0] & MOTOR_ENABLE;
        hardware->enable_saved = 1;
        hardware->gpio[0] |= MOTOR_ENABLE;
        struct timespec delay = {0, 20000000L};
        while (nanosleep(&delay, &delay) < 0) {
            if (errno != EINTR) {
                result = -errno;
                break;
            }
        }
    }
    uint8_t id_low, id_high;
    if (result == 0)
        result = register_read(hardware, 0x00, &id_low);
    if (result == 0)
        result = register_read(hardware, 0x01, &id_high);
    if (result == 0 && (id_low != 0x81 || id_high != 0x6c))
        result = -ENODEV;
    for (unsigned int index = 0; result == 0 && index < 3; ++index) {
        uint8_t mode;
        result = register_read(hardware, index == 2 ? 0x10 : channel_base(index), &mode);
        if (result == 0 && (mode & 0x80U))
            result = -EBUSY;
    }
    if (result == 0)
        result = register_read(hardware, 0x02, &hardware->original_global);
    if (result == 0) {
        hardware->global_saved = 1;
        result = register_checked(hardware, 0x02, 0x8a, 1);
    }
    if (result < 0)
        motor_hw_close(hardware);
    return result;
}

int motor_hw_begin(struct motor_hw *hardware, unsigned int index, unsigned int reverse,
                   unsigned int count, unsigned int agc, uint32_t microsteps)
{
    unsigned int base = channel_base(index);
    hardware->touched |= 1U << index;
    int result = motor_hw_stop(hardware, index);
    const unsigned int addresses[] = {2, 5, 3, 4, 6, 8, 10, 12, 14};
    const unsigned int values[] = {0x80, agc, 0, 0, count, 0, 0,
                                   microsteps ? microsteps % 256U : 0,
                                   microsteps ? microsteps / 256U : 65535U};
    for (unsigned int offset = 0; result == 0 && offset < sizeof(addresses) / sizeof(addresses[0]); ++offset)
        result = register_checked(hardware, base + addresses[offset], values[offset], offset >= 4 ? 2 : 1);
    if (result == 0)
        result = register_checked(hardware, base, 0x87, 1);
    if (result == 0)
        result = register_checked(hardware, base + 2, reverse ? 0x82 : 0x81, 1);
    return result;
}

int motor_hw_set_speed(struct motor_hw *hardware, unsigned int index, unsigned int count)
{
    return register_checked(hardware, channel_base(index) + 6, count, 2);
}

int motor_hw_sample(struct motor_hw *hardware, unsigned int index, uint32_t *position, int *ended)
{
    uint8_t global, state;
    int result = register_read(hardware, 0x04, &global);
    if (result == 0)
        result = register_read(hardware, channel_base(index) + 1, &state);
    if (result < 0)
        return result;
    if ((global & 0xf0U) != 0x90U || !(hardware->gpio[0] & MOTOR_ENABLE))
        return -EIO;
    *ended = !!(state & 1U);
    unsigned int base = channel_base(index);
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        unsigned int before, phase, after;
        result = register_word(hardware, base + 10, &before);
        if (result == 0)
            result = register_word(hardware, base + 8, &phase);
        if (result == 0)
            result = register_word(hardware, base + 10, &after);
        if (result < 0)
            return result;
        if (before == after && phase < 256U) {
            *position = after * 256U + phase;
            return 0;
        }
    }
    return -EIO;
}

int motor_hw_refresh(struct motor_hw *hardware, unsigned int index)
{
    unsigned int address = channel_base(index) + 10;
    int result = register_write(hardware, address, 0, 2);
    uint8_t high;
    if (result == 0)
        result = register_read(hardware, address + 1, &high);
    if (result == 0 && high >= 2)
        result = -EIO;
    return result;
}
