# 文件结构
* main.c:方便测试使用
* i2c_controller.c\h：Linux通用的SPI通讯函数
* tmi8160i_helper.c\h:几种电机典型的控制的函数封装，实质是调用封装好的电机API，如自动控制模式等
* tmi8160i_extension.c\h:一些不常见的API扩展的封装函数，是对基础API的进一步封装
* tmi8160i.c\h:**核心**的API封装，基本上是对电机所有寄存器的封装操作

## API使用流程
1. 把tmi8160i.c\h文件拷贝到自己的工程
2. 把tmi8160i.c文件中的write_byte、read_byte、write_2byte、read_2byte函数内部调用的I2C通讯接口换成自身
3. 如需要快速实现电机控制，可以考虑使用tmi8160i_helper.c\h文件，在调用该文件中的封装函数，如想自己控制，则无需其他文件
4. 如还需要其他扩展功能，则拷贝tmi8160i_extension.c\h文件
