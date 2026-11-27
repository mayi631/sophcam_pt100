# PT100 固件内嵌马达 API

## 范围

以 taixin `script/print_sim.lua` 和 `script/motor.lua` 实际使用的马达能力为参考，
本模块只实现底层马达 API，不绑定 UI、图像发送、pass 调度或纸张传感器。
上层模拟打印流程现由 `printmng.h` 提供，拍立得业务应使用打印管理接口，
而不是自行拼接马达启停；参见 `docs/pt100_print_api.md`。真实打印头仍需接入后端。

公共头：`applications/manager/include/motormng.h`。
实现：`applications/manager/src/motor/motormng.c`、`motor_hw.c`。
代码运行在调用应用自己的进程内，由工作线程轮询 TMI8160I；
不执行 shell、`system()`、`fork()` 或外部调试程序，不使用调试工具的控制 socket。
旧独立调试程序、后台程序 API 实验及开发阶段的 C 测试目录已清理，不参与正式固件构建。
独立免编译测试入口 `applications/tools/test_scripts/sample_motor` 已改为直接使用系统工具访问硬件，
不依赖外部马达可执行文件；用法见 `docs/sample_motor.md`。它与正式 API 共享排他锁，不可同时运行。

## 固件集成

`CONFIG_PT100_MOTOR=y` 时，模块编入 `libapp_mng.a` / `libapp_mng.so`，
主应用链接时保留 `MOTORMNG_Init` 所在模块，确保即使业务暂未调用也实际包含马达代码。
PT100 defconfig 和当前本地配置已开启；其他板型默认关闭。
正常 `make` 构建应用，`make install` 随 `sophcam` 安装到固件 data 分区暂存目录。
**不需要单独向板上复制一个驱动程序。**

接口在固件中不意味着自动运行：没有注册开机运动，没有新增打印业务调用点，
应用后续显式调用 `MOTORMNG_Init()`、`MOTORMNG_Start()` 等接口才会控制硬件。
`Init()` 仅检查引脚复用、占用共享 IC、读取 ID、准备全局配置和工作线程，不发送运动命令。
前提是没有其他控制者留下已启用的输出；读取 ID 需临时使能芯片，发现输出已被启用会拒绝接管。
应用关机/退出时应显式 `StopAll()` / `DeInit()`；不替换应用原有信号处理器。

## 与 taixin 模拟打印的对应关系

| taixin 使用的能力 | 固件 C 接口 |
| --- | --- |
| `motor.init()` / IC 初始化 | `MOTORMNG_DefaultConfig()`、`MOTORMNG_Init()`、`MOTORMNG_DeInit()` |
| `rotateLeft/rotateRight` / `rotateAuto` / `paperFeed` | `MOTORMNG_Start(channel, direction)` |
| `brakeCh` / `stop` / `stop2` | `MOTORMNG_Stop(channel)`、`MOTORMNG_StopAll()` |
| `setPrintHeadSpeed` / 原始速度计数 | `MOTORMNG_SetSpeed(channel, full_step_pps)` |
| 按线速度设置 | `MOTORMNG_SetLinearSpeedUm(channel, um_per_second)`，要求当前机构标定 |
| `rotateCyclesRel` / 非整圈定量走纸 | `MOTORMNG_MoveRelative()`、`MOTORMNG_MoveCyclesPhase()` |
| `rotateCyclesPhase` / 圈+相位移动 | `MOTORMNG_MoveCyclesPhase()`，明确本驱动 64 细分相位单位 |
| 清零坐标 / `resetCycleCount` | `MOTORMNG_SetPosition(channel, 0)`，仅停机时设置软件参考 |
| 累计目标走纸 / 回到起始坐标 | `MOTORMNG_MoveTo(channel, absolute_microsteps)` |
| `getCycleCount` / 运动完成查询 | `MOTORMNG_GetStatus()`、`MOTORMNG_WaitDone()` |
| 限位事件中停止 | `MOTORMNG_NotifyLimits()`，由后续传感器模块在任务线程上下文调用 |

没有直接照搬 `rotateLeft`、`paperFeed` 等物理命名：旧 taixin 的 CH12 是横移、CH34 是走纸，
且旧代码记录正反方向与左右物理方向相反；当前只验证了 CH12 接的这只马达。
因此 API 接受明确的 CH12/CH34 与正反方向，物理左右、送纸/退纸映射留给确认接线后的上层。
**默认 CH12 启用、CH34 禁用**，访问禁用通道返回 `-ENODEV`；确认第二路后才显式配置启用。
两个通道的状态、方向和目标独立，但共享 IC/EN；芯片/总线故障时停止全部已启用通道。

## 默认参数

| 项目 | 默认值 |
| --- | --- |
| 硬件 | SM10，TMI8160I，I²C0，7 位地址 `0x56` |
| 默认启用 | CH12；CH34 保留接口但禁用 |
| 速度 | 从 200 pps 按不超过 100 pps/s 加速到 510 pps，约 3.1 秒 |
| 510 pps 计数 | `SPEED_CNT=858`，28 MHz 标称时钟下约 509.91 pps |
| 调速范围 | 200～510 全步/秒；停止时也可设置下次运动的速度 |
| 幅值 | 150 mA 估算档，AGC `0x20`；初始化可选 100/125/150 档 |
| 估算参数 | VM 上界 5500 mV、相电阻下界 9300 mΩ |
| 模式 | 相对位置模式，64 微步/全步，PWM 约 109375 Hz |
| 连续运行 | 不定时停止，显式 Stop；总线/芯片故障仍停止 |
| 有限移动 | 硬件目标到达后结束，工作线程释放绕组；不留保持电流 |

150 档不是测到的每相 150 mA，也不是硬件恒流限制。SM10 每相 200 mA 的规格边界不变，
整板供电电流和手感温度不能替代相电流波形及温升验证。新固件后端仍须实板验证，
主机模拟测试不证明实际无失步、每相电流合规或经过齿轮达到 0.1 m/s。

## 调用约定与单位

所有 `int` 接口：成功 `0`，失败为负 errno；可用 `strerror(-result)` 显示原因。
`Start` 和各种 `Move` 是非阻塞运动提交：芯片接受配置后返回，不等待转动完成。
已有运动时再次启动/定量移动返回 `-EBUSY`，不直接反向；先 Stop 再发新运动。
运行中 `SetSpeed` 保留幅值和方向，平滑过渡；停止时只修改目标，不自行启动。
幅值、通道启用和机械标定只在初始化配置中修改，需要停机 DeInit 后重新 Init。

本马达/细分配置统一使用以下单位，不能直接抄旧代码的数字：

```text
1 全步 = 64 microsteps
1 TMI 电周期 CYCLE = 4 全步 = 256 microsteps
1 SM10 机械整圈 = 20 全步 = 1280 microsteps = 5 CYCLE
MoveCyclesPhase 的 phase 范围 = 0..255（不是旧文档中的 0..1023）
```

`MoveRelative(channel, signed_microsteps)` 中正数为正转、负数为反转；零/超出范围拒绝。
单次最大 `65535 CYCLE + 255 phase = 16777215 microsteps`，不会静默截断或自动拆段。
有限移动也从 200 pps 渐变，短行程可能还未升到目标速度就完成；到达目标后直接释放，不做主动保持。
Stop 同样立即释放，不是持续通电刹车。若后续机械需要保持力矩，必须另外设计电流和热约束。

`MoveTo()` 使用软件累计的控制坐标，不直接依赖旧版绝对位置模式的去程/回程特例。
初始化时没有物理零点，必须由调用者确认参考点后停机调用 `SetPosition()`，才允许 MoveTo。
`SetPosition()` 不会驱动马达、不会实际回零，不允许在运动中改变参考坐标。
没有编码器：位置是芯片已输出的微步计数，不是转子/纸张实测位置，失步仍可能发生。

连续模式每秒只清电周期计数以避免有限预算结束，不清相位、不重新启停输出。
清计数与读数不具备原子锁存能力，累计坐标可能丢失读写窗口内的控制步数；
因此连续刷新后 `position_referenced=0`，禁止把它当作精确绝对坐标继续 MoveTo。
需要精确定量/累计走纸应使用有限 Move 系列，或者后续接编码器/限位重新建立参考。

`travel_um_per_motor_rev` 默认为 0，表示当前机构尚未标定。
`MoveDistanceUm()` / `SetLinearSpeedUm()` 在未标定时返回 `-ENODATA`，不会套用 taixin 的旧机构比例。
标定值是**马达机械轴转一圈后末端实际移动的微米数**，已包含齿轮传动。
距离换算四舍五入到微步，线速度向下取整到 pps，换算后仍检查有效范围。
例如 0.1 m/s 对应 `100000 um/s`，只有有正确标定、且所需频率在 200～510 范围内才接受。

## 状态、等待和限位

`GetStatus()` 是线程安全的状态快照，不会触发运动。字段区分目标速度、最近下发的斜坡速度、
速度计数、幅值估算、软件控制坐标、参考点是否有效、停止原因和最后错误。
运行状态：`IDLE / RUNNING / DONE / FAULT`。DONE 只证明硬件命令预算完成，不是转子到位反馈。

`WaitDone(channel, timeout_ms)` 等待当前通道停止，超时范围 0～3600000 ms。
超时返回 `-ETIMEDOUT`，**不会自行停止马达**；调用者若要取消应明确 Stop。
有限运动被手动 Stop 或限位中断返回 `-ECANCELED`，不伪装成目标到达。
连续运动收到正常 Stop 后可返回成功。故障返回相应错误。
等待使用单调时钟，不受板端校时影响；等待期间重新初始化或提交了另一条运动时，不把新任务当成原任务。
等待/停止/初始化均可能阻塞，不能在 ISR、信号处理器或 UI 绘制函数中调用。

`NotifyLimits(channel, negative_active, positive_active)` 更新当前两端限位状态，
若正在朝激活的那一端运动则同步停机；之后禁止朝该端启动，允许朝离开限位的方向运动。
限位负端对应 REVERSE、正端对应 FORWARD，由后续传感器接入者映射实际开关。
该接口不会自行读取 GPIO，也不会自动寻找零点或将位置设为零。
目前没有接限位/纸张检测/编码器/温度反馈，不能把“预留入口”当作已具备这些保护。

I²C 错误、读回不一致、意外硬件结束、位置计数倒退等会故障停机并拉低共享 MOT_EN。
FAULT 后不允许继续运动，需处理原因后 DeInit / Init；不会自动重复试转。
与调试工具共享 `/tmp/pt100-motor.lock`，防止同时控制；不能阻止其他程序/小核绕过该锁。
BSP 仍将 MOT_EN 对应引脚用于旧 `sensor_RST` 流程，不能同时做相机复位和马达运行。
工作线程并非独立硬件看门狗，进程崩溃不能保证立即断流；现场仍需可断电、有人监测。

## 最小 C 调用示例

以下仅展示可单独调用的接口，不是打印流程。每个返回值都应由业务检查并处理。

```c
#include "motormng.h"

int example_motor_init(void)
{
    return MOTORMNG_Init(NULL);
}

int example_motor_start(void)
{
    return MOTORMNG_Start(MOTORMNG_CH12, MOTORMNG_FORWARD);
}

int example_motor_stop(void)
{
    return MOTORMNG_Stop(MOTORMNG_CH12);
}

int example_motor_one_revolution(void)
{
    return MOTORMNG_MoveCyclesPhase(MOTORMNG_CH12, 5, 0, MOTORMNG_FORWARD);
}
```

其他直接调用：

```c
MOTORMNG_SetSpeed(MOTORMNG_CH12, 405);
MOTORMNG_MoveRelative(MOTORMNG_CH12, -1280);
MOTORMNG_SetPosition(MOTORMNG_CH12, 0);
MOTORMNG_MoveTo(MOTORMNG_CH12, 1280);
MOTORMNG_WaitDone(MOTORMNG_CH12, 3000);
MOTORMNG_NotifyLimits(MOTORMNG_CH12, 0, 1);
MOTORMNG_StopAll();
MOTORMNG_DeInit();
```

最后一段是独立接口列表，不能不检查返回值就作为连续流程照抄执行。
如果需要 CH34，先 `DefaultConfig()`，确认接线后设置 `config.channel[1].enabled=1` 再 Init。
应用已经包含 manager 头搜索路径和 `-lapp_mng` / pthread，无需另加运行时程序路径。

## 独立脚本回归测试

```sh
sh -n applications/tools/test_scripts/sample_motor
sh applications/tools/test_scripts/tests/test_sample_motor.sh
MOTOR_TEST_ASH=1 sh applications/tools/test_scripts/tests/test_sample_motor.sh
```

当前保留的回归测试使用假 GPIO/I²C 验证独立脚本的寄存器配置、200→510 渐变、持续运行、
手动停止、周期刷新、互斥、反转、探测、参数校验及故障清理。
这些测试不访问真实硬件，也不代替正式 C API 的行为或板端实转验证。
