# 交叉编译器设置
CROSS_COMPILE = arm-buildroot-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc

# 目标名称
TARGET = tmi8160i

# 源文件
SRCS = main.c i2c_controller.c tmi8160i.c tmi8160i_helper.c tmi8160i_extension.c 

# 头文件目录
INCLUDES = -I.

# 编译输出目录
BUILD_DIR = build

# 对象文件（自动放在 build/ 目录下）
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# 编译选项
CFLAGS = -Wall -O2 $(INCLUDES)
LDFLAGS = 

# 默认目标
all: $(BUILD_DIR) $(TARGET)
-
# 创建 build 目录（如果不存在）
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 生成可执行文件
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# 生成 .o 文件（自动放在 build/ 目录下）
$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean