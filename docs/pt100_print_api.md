# PT100 模拟打印与打印头接入 API

## 当前能力和边界

公共头：`applications/manager/include/printmng.h`。
实现：`applications/manager/src/print/`，通过 `motormng.h` 控制马达，不执行外部脚本。

| 模式 | 马达 | 图像、打印头 |
| --- | --- | --- |
| `PRINTMNG_DRY_RUN`（默认） | 不初始化、不访问 GPIO/I²C | 模拟扫描位置和数据列进度，不解码图片、不输出打印数据 |
| `PRINTMNG_MOTOR_SIMULATION` | 只驱动显式启用且有机构标定的轴，其他轴仍模拟 | 按命令微步坐标核算数据窗口，不发送打印头数据 |
| `PRINTMNG_REAL_PRINT` | 必须启用扫描、走纸两轴，必须物理归零 | 必须完整提供图像、打印头、归零和安全后端，否则拒绝初始化 |

本次完成的是模拟流程和真实接入契约，**不是已经能实际出图的打印头驱动**。
没有接 UI 拍照按钮、开机自动打印或开机自动运动；后续拍立得在保存图片后调用 `Submit()` 即可。
纯模拟允许图片路径为空或不存在，它只保留路径元数据；真实模式要求非空路径，
文件存在性、格式、解码、裁剪、颜色处理、半色调及逐 pass 数据准备由 `prepare_image` 验证、实现。

## 与 taixin 的对应

参考原文件 `taixin/PT03/git_printer/printer/script/print_sim.lua` 的完整模拟流程，
不是文件开头当前开启的 `DIRECT_FEED_TEST` 连续转动捷径。没有移植 taixin 的自动启动行为。

流程：

```text
Submit → PREPARING → PREFEED → HOMING
  → [SETTLING → SCANNING（左右交替）→ SETTLING → FEEDING] × sweeps
  → 可选 EJECTING → finish → 释放马达 → COMPLETED
取消/错误 → 真实后端 abort → 释放全部自有马达 → CANCELLED / FAILED
```

- 预走纸在归零之前；每次扫描之后走纸，包括最后一次扫描。
- `passes=8/16` 表示交错模式，不是总共只扫描 8/16 次。
- `sweeps = ceil(feed_length_um * passes / head_width_um)`。
- `columns_per_sweep = round(scan_width_um * 1000 * (passes/2) / nozzle_pitch_nm)`。
- `image_rows = round(feed_length_um * 1000 / nozzle_pitch_nm)`。
- 第 n 次扫描后的累计走纸量为 `round(n * head_width_um / passes)`，避免逐次取整产生累积偏差。
- 最终目标走纸量 = 预走纸 + 全部扫描的累计走纸 + 可选出纸；不是恰好等于图片长度。
- `Sweep.index` 从 1 开始，`interlace_phase` 从 0 开始；`feed_offset_um` 不包含预走纸。
- 每个方向各有数据起点。`data_start_um` / `data_end_um` 均是**从该趟扫描起点沿行进方向**的距离，
  不是固定从左侧测量。RTL 的实际像素顺序及交错喷孔映射由真实后端处理。

`DefaultJob()` 是 taixin 的几何示例：图宽 44000 um、图长 60000 um、头宽 8128 um、
扫描行程 60000 um、双向数据起点各 5500 um、预走纸 15000 um、喷孔间距 42330 nm、出纸 0。
8-pass 为 60 趟、每趟 4158 列、总走纸 75960 um；16-pass 为 119 趟、8316 列、75452 um。
这些数值用于验证流程，**不是 PT100 已标定的机构尺寸或打印头规格**。
真实装机前须根据新机构、打印头修改 Job 和轴标定，不复用旧的齿轮/编码器比例。

## 拍立得调用接口

| 接口 | 用途 |
| --- | --- |
| `PRINTMNG_DefaultConfig()` / `DefaultJob()` | 填充默认配置/任务，默认无硬件 |
| `PRINTMNG_CalculatePlan()` | 只计算、校验任务，可先向 UI 展示预计趟数 |
| `PRINTMNG_Init()` | 建立管理线程；真实马达模式会初始化自有 MOTORMNG，不发运动指令 |
| `PRINTMNG_Submit()` | 异步提交一张图片/一次任务，返回 job_id，复制整个 Job（包含路径） |
| `PRINTMNG_GetStatus()` | 线程安全快照：状态、趟数、列数、命令走纸量、安全故障与错误 |
| `PRINTMNG_Cancel(job_id)` | 非阻塞请求取消；工作线程执行后端清理与马达停止 |
| `PRINTMNG_Wait(job_id, timeout_ms, &status)` | 在非 UI 线程等待任务结束 |
| `PRINTMNG_DeInit()` | 取消当前任务、等待清理、释放自有马达和线程 |

最小纯模拟业务接入（应用内直接调用，不依赖 `sample_print`）：

```c
#include "printmng.h"
#include <errno.h>
#include <string.h>

int instant_print_init(void)
{
    return PRINTMNG_Init(NULL);
}

int instant_print_photo(const char *saved_path, uint64_t *job_id)
{
    PRINTMNG_Job job;
    if (!saved_path || strlen(saved_path) >= sizeof(job.image_path))
        return -EINVAL;
    PRINTMNG_DefaultJob(&job);
    strcpy(job.image_path, saved_path);
    return PRINTMNG_Submit(&job, job_id);
}
```

UI 定时查询 `GetStatus()`，按 `state` 和 `sweeps_done / plan.sweeps` 更新进度；
取消按钮调用 `Cancel(job_id)`。应用退出在非 UI 绘制上下文调用 `DeInit()`。
当前为单实例、同一时刻仅一个任务，没有任务队列；运行中再次提交返回 `-EBUSY`。
仅保留最新任务状态，提交新任务后旧 job_id 的 `Wait()` 返回 `-ESTALE`。
`Wait()` 超时返回 `-ETIMEDOUT`，**不取消任务**，需检查 `status.active` 区分仍在运行与任务自身超时失败。
终态取消返回 `-ECANCELED`，错误返回负 errno。FAILED 后必须排查故障再 DeInit / Init，不能直接重试运动。
`last_error` 保留主错误，`cleanup_error` 保留 abort/马达停止错误；清理失败始终为 FAILED。

`Config` 和 `Backend` 函数表会复制，`context` / `status_context` 指向对象必须一直存活到 DeInit 返回。
后端及 `on_status` 均在打印工作线程、内部锁外调用，不是 UI 线程。
回调只能进行短时、有界操作，不得阻塞等待整个作业；不要在回调里调用 Wait / DeInit。
回调可 GetStatus / Cancel；转发 UI 消息时必须复制状态，不能保留传入指针。
Job/Plan/Sweep 参数也应复制到后端上下文，不应保留传入的临时对象地址。
状态回调在状态转换、走纸完成及任务终态触发；细粒度列数进度通过 GetStatus 查询。
后端若需要长时间处理，须自行设计有界/异步机制，避免延迟取消和安全轮询。

## 开启真实马达的模拟打印

默认不假定 CH12/CH34 的实际物理角色。taixin 的 CH12 扫描、CH34 走纸仅作为配置模板；
PT100 当前只有 CH12 的单只马达验证记录。默认两个轴都不启用。

```c
PRINTMNG_Config config;
PRINTMNG_DefaultConfig(&config);
config.mode = PRINTMNG_MOTOR_SIMULATION;
config.feed_axis.enabled = 1;
config.feed_axis.channel = MOTORMNG_CH12;
config.feed_axis.travel_um_per_motor_rev = measured_feed_um_per_rev;
config.feed_axis.forward_direction = MOTORMNG_FORWARD;
int result = PRINTMNG_Init(&config);
```

此例只有走纸电机实动，扫描和归零仍是软件模拟。每个轴都可设置通道、方向、速度、幅值估算、
`travel_um_per_motor_rev`（马达机械轴一圈后末端实际移动微米数，包含齿轮传动）。
未标定返回 `-ENODATA`；两个角色不能占用同一通道。距离、单次微步预算和分辨率均校验。
扫描、预走纸、步进走纸的每段动作受 `move_timeout_ms` 限制，按实际机构行程/速度设置足够的超时。

默认目标仍为已经验证的 510 pps / 150 mA 幅值估算，由底层从 200 pps 渐变。
150 mA **不是实测、闭环恒流限值**；SM10 每相 200 mA 上限不变，整板供电电流不能代替相电流。
短行程可能在达到 510 pps 前就结束，打印管理器不提高现有马达安全范围。

扫描轴必须提供 home 三个回调；只有 MOTOR_SIMULATION 可用 `allow_unhomed_scan=1` 显式豁免：
使用者须先停机手动放在左侧起点、确认正方向和足够行程，再提交任务。
此时仅设软件零点，**不会寻找限位**，`physical_home_verified=0`。每次新任务前都要重新确认起点。
真实打印不允许此豁免。走纸轴每个作业建立相对软件零点，不代表已检测纸张位置。

模块独占自己初始化的 MOTORMNG。若此前其他模块已初始化马达则返回 `-EALREADY`，
不接管、不停止或反初始化别人的实例；打印期间不能再直接发送其他 MOTORMNG 命令。
原 `sample_motor`、其他控制进程不能并行运行。共享 MOT_EN 与 BSP 旧 sensor_RST 冲突仍需遵守：
马达运行时不得复位相机。当前无外部硬件看门狗，进程崩溃/阻塞不能保证及时断流，现场须可断电。

## 真实打印头接入点

设置 `mode=PRINTMNG_REAL_PRINT`，确认双轴标定，提供以下完整 Backend 后再启用真实打印：

| 回调 | 接入职责 |
| --- | --- |
| `check_safety` | 读取真实纸张、盖子、温度、卡纸、传感器有效性，填故障位；非零故障即中止 |
| `home_begin` | 使用管理器已拥有的 MOTORMNG 启动限位寻零，不要再次 Init；按传入通道、朝左方向执行 |
| `home_poll` | 非阻塞读取限位/位置，只有确实到达物理左参考点才设 complete |
| `home_end` | 无条件停止寻零，包括 begin 失败/取消/超时，成功返回前必须停住轴 |
| `prepare_image` | 校验/打开图片，建立颜色/半色调/交错光栅，准备头电源与缓存 |
| `arm_sweep` | 在马达启动前配置当趟方向、数据窗口、phase、feed_offset 和列数，布置触发硬件 |
| `poll_sweep` | 返回真实累计已发送列数及 complete；列数必须单调、不超目标，complete 时须恰好等于目标 |
| `end_sweep` | 在扫描结束后关闭当趟输出、收尾；必须在走纸之前完成 |
| `finish` | 整个作业成功后关闭喷射/发热输出，释放图片和 DMA/定时资源 |
| `abort` | 任意失败/取消都可调用，包含 prepare/arm 部分失败；幂等关闭头输出、释放资源 |

返回值 0 成功、负 errno 失败；正数按 `-EIO` 处理。
`home_end` 在调用过 home_begin 后总会执行。`abort` 失败也继续尝试停止全部自有马达。
finish 成功应已将头置于安全状态，即使之后马达停止失败也不能遗留有效喷射/发热输出。
取消/安全故障检查发生在各阶段和轮询间；阻塞回调无法被线程强行中断，不能当作硬实时保护。

**打印头协议、喷孔/加热时序、供电保护、编码器、限位和纸张传感器尚未实现。**
Linux 线程的毫秒轮询只做管理，不能用它按列 sleep 来直接驱动高速打印头。
真实列触发必须在 MCU / 硬件定时器 / DMA / 编码器同步引擎中完成；
arm_sweep 提供几何与任务元数据，poll_sweep 读取该引擎实际统计。
扫描命令结束但真实列数不足时作业失败，不补造已打印列数。
马达位置、feed_commanded_um 和模拟列数都是命令统计，**不证明真实纸张/转子到位或真实出图**。

## 编译、独立验证

`CONFIG_PT100_PRINT=y` 依赖 `CONFIG_PT100_MOTOR=y`，已在 PT100 defconfig 开启。
管理器编入 `libapp_mng`，主应用显式保留 PRINTMNG 模块，不需要先运行外部服务。
正常应用构建会生成 `out/bin/sample_print`，安装到 `out/install/bin/sample_print`，随固件应用安装流程打包。
默认不自动部署到当前连接板，也不改动开机脚本。

板端纯模拟（新固件正常安装后，或手工复制 ARM 可执行文件）：

```sh
/mnt/data/bin/sample_print
/mnt/data/bin/sample_print --passes 16 --image /mnt/sd/photo.jpg
/mnt/data/bin/sample_print --fast
```

实际安装挂载路径以固件 data 分区布局为准；若直接复制到 `/mnt/data/sample_print` 则使用该路径。
`--fast` 只缩短软件模拟/等待，不提高马达速度，也不设置打印头时序。
慢速/长行程可通过 `--move-timeout-ms` 设置单段运动超时（最大 3600000 ms）。
Ctrl+C 请求取消并等待释放，不要使用 kill -9 作为常规停止方式。

明确标定后，单 CH12 走纸实动示例（扫描仍模拟；默认 Job 仍为 taixin 几何示例）：

```sh
/mnt/data/bin/sample_print --feed-channel 12 \
  --feed-um-per-rev "${FEED_UM_PER_REV:?请先填写实测机构标定}" --pps 510 --current-ma 150
```

不要猜测 FEED_UM_PER_REV。命令行用 `--help` 查看双轴/反转选项；真实打印头只能通过业务代码注册后端，
测试程序没有“跳过后端直接真实打印”的开关。`sample_print` 是编译后的 C 示例，不依赖旧 pt100_motor。

主机回归测试（假马达和假打印头，不访问板端）：

```sh
make -C applications/manager/tests/print check
make -C applications/manager/tests/print check \
  BUILD=/tmp/pt100-print-tests-asan \
  EXTRA_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
```

覆盖 8/16-pass 计划、纯模拟零硬件访问、方向/phase/累计走纸、互斥、取消、超时、
单轴/双轴适配、未标定/未归零拒绝、所有权冲突、归零清理、安全故障、打印列不足/超报和清理错误。
这些测试不代替真实马达、纸路、打印头的现场联调。
