# sample_motor 独立板端马达测试

## 部署与命令

只需将 `applications/tools/test_scripts/sample_motor` 复制到板端 `/mnt/data/sample_motor`。
这是直接通过系统工具访问 GPIO 和 I²C 的 shell 脚本，不需要编译，不依赖
`pt100_motor` 可执行文件、该工具的源码目录、固件中的 `MOTORMNG_*` API、MCU 或屏幕。

在板端 root 串口或 ADB shell 中执行，先确保机构可以安全连续运动：

```sh
sh /mnt/data/sample_motor probe
sh /mnt/data/sample_motor
```

第二条启动后台持续正转：CH12、150 mA 幅度估算档、从 200 pps 逐渐加速到 510 pps。
启动命令返回只说明寄存器配置已成功，不表示转子已转动或达到目标速度。
运行中可分别执行：

```sh
sh /mnt/data/sample_motor status
sh /mnt/data/sample_motor speed 405
sh /mnt/data/sample_motor speed 510
sh /mnt/data/sample_motor stop
```

正常运行没有定时自动停机，必须输入 `stop`；关闭终端、ADB 断开或启动命令返回后按
Ctrl-C 都不会停止后台马达。故障、退出信号或初始化失败会执行停止与断流清理。
`stop` 等待后台确认释放线圈；若失败或无法确认，检查日志并在必要时断开马达电源。
启动确认和停止确认各有约 10 秒的轮询上限，不限制正常运动持续时间。

从较低速度开始或反转（反转前先停止）：

```sh
sh /mnt/data/sample_motor start 200
sh /mnt/data/sample_motor stop
sh /mnt/data/sample_motor start 200 reverse
sh /mnt/data/sample_motor stop
```

`help` 仅显示帮助；`probe` 临时使能芯片读取 ID/寄存器后恢复 MOT_EN，不发运动指令。
运行中禁止重新启动、直接换向或探测；调速范围为 200～510 全步/秒。
`status` 显示最后下发的速度与目标速度，不是编码器实测转速。
默认状态与日志在 `/tmp/sample-motor/`，日志为 `/tmp/sample-motor/motor.log`。
`SAMPLE_MOTOR_STATE_DIR` 可指定其他绝对路径；同一次测试的全部命令必须使用同一个目录。

## 板端条件

需要 root，以及系统工具 `devmem`、`i2ctransfer`、`flock`、`setsid`、`usleep`、
`readlink` 和基本文件工具；使用 `/proc/uptime` 作为单调时钟。
当前 PT100 BSP 的 `pt100_bsp/manifest/busybox_pt100_config` 已启用这些 BusyBox 功能。
缺少工具会直接报错，不会回退到满幅输出或调用旧驱动。

硬件固定为 I²C0、7 位地址 `0x56`、TMI8160I ID `0x6c81`、CH12。
MOT_EN 必须已经复用为 PWR_GPIO4 并设为输出；脚本只检查，不修改复用和方向。
脚本使用与正式固件相同的 `/tmp/pt100-motor.lock` 排他锁，防止同时控制芯片。
固件若已调用 `MOTORMNG_Init()`，先在应用中释放所有权，再运行脚本。
相机复位与 MOT_EN 有引脚冲突，测试时禁止相机复位操作；共享锁不能约束未使用它的代码。
不添加开机运动，不修改固件业务或打印流程。

## 驱动参数与安全边界

寄存器配置顺序：先禁止输出、全局配置 `0x82`、CH12 AGC `0x20`、
时钟/速度分频均为 0、速度计数初值 2188、相位与周期计数清零、目标周期 65535，
再设置全局 `0x8a`、CH12 相对位置模式 `0x87`、前进 `0x81` 或后退 `0x82`。
使用 64 微步/全步，速度计数为 `ceil(437500 / pps)`；510 pps 对应 858。
读回校验配置写入，写入失败不重放运动命令。

循环约每 100 ms 轮询状态，用单调时间限制加减速不超过 100 pps/s；
每次最多变化 10 pps，系统繁忙时实际加速会更慢。
约每秒只清零周期计数 `0x2a/0x2b`，不清零相位，不重复发送启动指令，
防止相对位置的硬件目标周期自然耗尽。脚本不提供精确位置/走纸长度计量。

检测全局复位、休眠、驱动芯片过温、振荡器异常、CH12 提前结束、MOT_EN 被拉低、
I²C 读写或读回失败后停止输出，不自动重试或重新启动。
正常停止将 CH12 设为开路并关闭通道，恢复原全局配置和原 MOT_EN 位，保留 GPIO 其他位；
故障或清理失败时尝试强制拉低共享 MOT_EN。

**AGC `0x20` 只是根据 VM 上界 5.5 V、线圈电阻下界 9.3 Ω 推算的 150 mA 幅度档，
不是硬件恒流值或实测相电流。** SM10 规格限制 200 mA/相；整板电源读数不能替代相电流。
不提供继续加流/超过 510 pps 的参数。没有转子、堵转、马达温度或相电流反馈，
驱动芯片过温标志也不等于马达温度保护。必须有人观察温升和机构行程，异常时立即停止/断电。
不要 `kill -9` 后台进程：这种信号无法执行清理，线圈可能仍通电；状态残留时不能据此认定已停机。
掉电后 `/tmp` 状态会丢失，需要重新手动启动。

后续模拟打印仍调用 `motormng.h` 中的正式 C API；本脚本是独立人工测试工具，不是 API 桥接器。

## 无硬件验证

```sh
sh -n applications/tools/test_scripts/sample_motor
sh applications/tools/test_scripts/tests/test_sample_motor.sh
MOTOR_TEST_ASH=1 sh applications/tools/test_scripts/tests/test_sample_motor.sh
```

测试通过假 `devmem`/`i2ctransfer` 检查寄存器序列、渐变、连续刷新、手动停止、互斥、反转、
探测、参数校验、故障与退出清理，不访问真实 GPIO/I²C，不等于板端转速/温升验证。
测试保留在 `test_scripts/tests/` 用于脚本回归，不依赖旧调试程序，也不随脚本安装到板端。
