# caminfo.md — CAM_INFO / AI_CFG 分区说明与 DC100 移植指南

> 本文档由 DC309 工作区（`/home/mayi/SDK_CV184X_DC309`）会话生成。
> 目的：①记录 DC309 `CAM_INFO` 分区的创建方式与使用方法；②给出在
> `/home/mayi/SDK_CV184X_DC100/sophcam` 下创建类似分区（`AI_CFG`）保存动物识别
> `user_config.json` 的完整、可执行步骤，供下一个会话照做。
>
> 本会话已完成的准备工作（已生成文件见文末「移植包文件清单」）：
> - 新模块 `aicfgmng.c/h` 源码（已通过 DC309 交叉工具链 `-fsyntax-only -Werror` 语法验证）；
> - DC100 新版分区表 `partition_spinand.xml`（已用 DC100 的 `mkcvipart.py` 生成 cvipart.h 验证偏移正确）；
> - 集成补丁 `patch/sophcam_aicfg_integration.patch`（已用 DC100 真实文件副本 `patch -p1` 应用验证通过）。

---

## 1. 背景

| 项目 | DC309 | DC100 |
|---|---|---|
| SDK 根目录 | `/home/mayi/SDK_CV184X_DC309` | `/home/mayi/SDK_CV184X_DC100` |
| sophcam 目录 | `sophcam_v20260623` | `sophcam` |
| 板级配置 | `cv1842cp_dc309_spinand` | `cv1842cp_dc100_spinand` |
| 分区表 | `build/boards/cv184x/cv1842cp_dc309_spinand/partition/partition_spinand.xml` | `build/boards/cv184x/cv1842cp_dc100_spinand/partition/partition_spinand.xml` |
| 已有专用 mtd 分区 | `CAM_INFO`（512KB，相机校准信息） | 无 |
| 待保存文件 | — | `oss/kt_ani_sdk/install/arm-none-linux-musleabihf/bin/user_config.json`（动物识别授权配置，约 150B JSON） |

---

## 2. DC309 `CAM_INFO` 分区机制（调研结论）

### 2.1 分区的创建方式

分区由 **分区表 XML + 生成脚本** 定义，不手工改 dts：

1. **分区表源文件**（唯一需要人工维护的文件）：
   `build/boards/cv184x/cv1842cp_dc309_spinand/partition/partition_spinand.xml`
   关键行（位于 `ENV_BAK` 之后、`ROOTFS` 之前）：
   ```xml
   <partition label="CAM_INFO" size_in_kb="512" readonly="false" file="" mountpoint="" type=""/>
   ```

2. **构建时自动生成 cvipart.h**：
   - 生成脚本：`build/tools/common/image_tool/mkcvipart.py`（读取 XML，输出 `cvipart.h` 和可选 `fw_env.config`）
   - 调用点（构建系统内）：
     - `build/scripts/fip.mk` → 输出到 `${FSBL_PATH}/build/cvipart.h`
     - `build/Makefile` → 输出到 `${UBOOT_PATH}/include/cvipart.h`
     - `build/riscv.mk` → 输出到 `${OPENSBI_PATH}/include`
   - 生成的 `PART_LAYOUT`（uboot 传给内核的 mtdparts）：
     ```
     "mtdparts=cvsnfc:2560K(fip),3072K(2nd),8192K(BOOT),640K(MISC),128K(ENV),128K(ENV_BAK),512K(CAM_INFO),40960K(ROOTFS),64000K(DATA)"
     ```
   - 生成的偏移宏：`CAM_INFO_PART_OFFSET=0xe60000`、`CAM_INFO_PART_SIZE=0x80000`

3. **内核侧无需修改**：mtdparts 由 uboot 通过 cmdline 传入，内核据此创建
   `/dev/mtdN` 节点，`/proc/mtd` 可查。用户态通过 `/proc/mtd` 按分区名找到
   `/dev/mtdN` 后直接 `open/ioctl/pread/pwrite` 访问。

4. **约束**：`caminfomng.c` 中 `CAMINFO_PARTITION_SIZE_MAX (0x80000)`（512KB）与
   分区大小一致，分区超限直接拒绝，防止误操作。

### 2.2 分区的使用方法（sophcam 侧）

代码位置：
- `sophcam_v20260623/applications/manager/src/caminfo/caminfomng.c`
- `sophcam_v20260623/applications/manager/include/caminfomng.h`
- 编译注册：`sophcam_v20260623/applications/manager/Makefile` 中
  `SRCS_C += $(SDIR)/caminfo/caminfomng.c`（链接进 `libapp_mng`，最终进 sophcam 可执行文件）

存储格式（头部 + 定长结构体 + CRC32）：
```c
typedef struct _CAMINFO_STORE_S {
    uint32_t magic;      /* 0x43414D49 'CAMI' */
    uint32_t version;    /* 1 */
    uint32_t data_size;  /* sizeof(cam_info_t) */
    cam_info_t info;     /* FocusPos / LscValid / MLSC / RLSC 等相机校准数据 */
    uint32_t crc32;      /* CRC32(从 magic 到 info 末尾，不含本字段) */
} CAMINFO_STORE_S;
```

对外 API：
| API | 说明 |
|---|---|
| `CAMINFOMNG_GetInfo(cam_info_t*)` | 读分区并校验 magic/version/data_size/crc32 |
| `CAMINFOMNG_UpdateInfo(const cam_info_t*)` | 整块擦除后写入 |
| `CAMINFOMNG_GetLscInfo/UpdateLscInfo` | 读改写 LSC 相关字段（先读后写，保留其他字段） |

实现要点（新模块照抄此模式即可）：
- 通过 `/proc/mtd` 逐行 `sscanf` 解析 `mtdN: size erasesize "name"`，匹配分区名得到 `/dev/mtdN`；
- `ioctl(fd, MEMGETINFO, &mtd)` 取 erasesize/writesize/size；
- `ioctl(fd, MEMGETBADBLOCK, &loff)` 判断坏块，坏块跳过（offset += erasesize）；
- 写流程：`ioctl(fd, MEMERASE, &erase)` 擦除整块 → 块缓冲填 `0xFF` 后拷入数据 →
  `pwrite` 整块写入；读流程：按块 `pread`；
- `pthread_mutex` 保护读写并发；
- 日志用 `CVI_LOGI/CVI_LOGW/CVI_LOGE`（来自 `common_files/cvi_log.h`）；
- 错误码用 `APP_APPCOMM_ERR_ID(APP_MOD_SYSTEM, ...)`（来自 `applications/include/appcomm.h`）。

调用方示例（DC309 dashcam）：
- `photomode.c`：开机/拍照时 `CAMINFOMNG_GetInfo` 读 FocusPos；
- `auto_calibration.c`：自动对焦后 `CAMINFOMNG_UpdateInfo` 持久化；
- `media_init.c`：`CAMINFOMNG_GetLscInfo` 恢复上次的 LSC 标定。

### 2.3 已知使用前提
- sophcam 以 root 运行（读写 `/dev/mtdN` 需要权限）；
- 分区未写过（全 0xFF）时 magic 校验失败 → 返回 `CAMINFOMNG_ENODATA`，调用方忽略即可。

---

## 3. DC100 移植方案（已与用户确认）

- 新分区名：**`AI_CFG`**，大小 **512KB**（与 CAM_INFO 一致），位置：`ENV_BAK` 之后、`ROOTFS` 之前。
- 用途：保存动物识别授权配置 `user_config.json`。
- 集成方式：**启动时自动恢复** —— `anip_service` 调用 `kt_ani_init()` 之前，
  从 `AI_CFG` 分区读出 `user_config.json` 恢复到 `/mnt/data/bin/ai_model/user_config.json`
  （仅当目标文件缺失或内容不一致时覆盖；分区无数据则保持现状）。
  授权时生成的 `run.json` 仍留在 `/mnt/data/bin/ai_model/`（可写目录），不受影响。
- 数据流：
  ```
  生产/升级：user_config.json ──AICFGMNG_UpdateFromFile()──▶ AI_CFG mtd 分区
  运行启动：AI_CFG 分区 ──AICFGMNG_RestoreConfig()──▶ /mnt/data/bin/ai_model/user_config.json
                                                         └─▶ kt_ani_init(config_dir=该目录)
  ```

---

## 4. DC100 执行步骤（给下一个会话）

以下所有路径均在 `/home/mayi/SDK_CV184X_DC100/` 下，sophcam 指 `sophcam/`。
**注意：当前会话无法写入 `/home/mayi/SDK_CV184X_DC100`（只读文件系统），以下步骤需由可写会话执行。**

### 步骤 1：修改分区表（源头）

用移植包中的 `partition_spinand.xml` **覆盖**：
`build/boards/cv184x/cv1842cp_dc100_spinand/partition/partition_spinand.xml`

或手工在 `ENV_BAK` 行之后插入一行：
```xml
<partition label="AI_CFG" size_in_kb="512" readonly="false" file="" mountpoint="" type=""/>
```

新布局（已用 DC100 的 `mkcvipart.py` 验证，生成结果如下）：
```
mtdparts=cvsnfc:2560K(fip),3072K(2nd),8192K(BOOT),896K(MISC),128K(ENV),128K(ENV_BAK),512K(AI_CFG),30720K(ROOTFS),57344K(DATA)
AI_CFG_PART_OFFSET=0xea0000   AI_CFG_PART_SIZE=0x80000
ROOTFS_PART_OFFSET=0xf20000   DATA_PART_OFFSET=0x2d20000
```
- ENV/ENV_BAK 偏移不变（0xe60000 / 0xe80000），uboot env 不受影响；
- ROOTFS/DATA 偏移后移 512KB。

**不要手工改** `fsbl/build/cvipart.h`、`u-boot-2021.10/include/cvipart.h`；
重新构建 fsbl/uboot 时 `mkcvipart.py` 会自动重新生成。如需立即预览，可手动运行：
```bash
python3 build/tools/common/image_tool/mkcvipart.py \
  build/boards/cv184x/cv1842cp_dc100_spinand/partition/partition_spinand.xml <输出目录>
```

### 步骤 2：添加 `aicfgmng` 模块（源码已备好，直接复制）

```
cp <移植包>/aicfgmng.h  sophcam/applications/manager/include/aicfgmng.h
mkdir -p sophcam/applications/manager/src/aicfg
cp <移植包>/aicfgmng.c  sophcam/applications/manager/src/aicfg/aicfgmng.c
```

模块说明：
- 存储格式（变长数据，适配 JSON 文本）：
  ```
  magic(4B 'AICF'=0x41494346) + version(4B=1) + data_size(4B) + crc32(4B) + data[data_size]
  crc32 覆盖 magic/version/data_size/data（12B + data_size），不覆盖 crc32 字段自身；
  分区内 data 之后剩余空间为 0xFF。
  ```
- API：
  | 函数 | 作用 |
  |---|---|
  | `AICFGMNG_Get(uint8_t* buf, size_t* len)` | 读分区，校验后返回数据（len 入参为缓冲区大小，出参为实际长度） |
  | `AICFGMNG_Update(const uint8_t* data, size_t len)` | 整块擦除写入（len ≤ 128KB） |
  | `AICFGMNG_LoadToFile(const char* path)` | 分区 → 文件（覆盖写） |
  | `AICFGMNG_UpdateFromFile(const char* path)` | 文件 → 分区（固化出厂配置用） |
  | `AICFGMNG_RestoreConfig(const char* path)` | 分区有数据且目标文件缺失/不一致时覆盖写；否则不动 |
- 依赖与编译选项：`appcomm.h`（`applications/include`）、`cvi_log.h`（`common_files`）、
  `mtd/mtd-user.h`；需要 `-D_GNU_SOURCE -std=gnu11`（sophcam `common_files/base.mk` 已带）。
- **已验证**：本会话用 DC309 交叉工具链
  `arm-none-linux-musleabihf-gcc -fsyntax-only -Wall -Wextra -Werror -std=gnu11 -D_GNU_SOURCE`
  编译通过，无警告。

### 步骤 3：注册到 manager 构建

`sophcam/applications/manager/Makefile`，在
`SRCS_C += $(SDIR)/appcomm/appcomm.c` 之后加一行：
```makefile
SRCS_C		+= $(SDIR)/aicfg/aicfgmng.c
```
（等效修改见移植包 patch。）

### 步骤 4：给 app_services 加 include 路径

`sophcam/app_services/Makefile`：
```makefile
# 在 CVI_MPI_DIR 定义后加：
MANAGEER_DIR			:= $(SRCTREE)/applications/manager
# 在 INCS 列表（如 KT_ANI_SDK_DIR 行后）加：
INCS		+= -I$(MANAGEER_DIR)/include
```

### 步骤 5：集成到 anip_service（恢复 user_config.json）

`sophcam/app_services/src/anip/anip_service.c`，两处修改：

1. 头部 include（`#include "anip_service.h"` 之后）：
   ```c
   #include "aicfgmng.h"
   ```
2. `anip_proc_init()` 函数内，`g_anip_init_done` 分支之后、
   `// 初始化动物识别引擎` / `kt_ani_set_log_level(ANI_LOG_LEVEL_WARN);` 之前插入：
   ```c
   /*
    * 从 AI_CFG mtd 分区(512KB)恢复 user_config.json 到 ai_model 目录。
    * 场景：/mnt/data 被格式化/升级覆盖后首次启动、或分区内容与现有文件不一致时，
    * 以分区固化为准覆盖写入；分区无有效数据(ENODATA)时保持现状不动。
    * 授权时生成的 run.json 仍留在 /mnt/data，不受影响。
    */
   {
       int32_t cfg_ret = AICFGMNG_RestoreConfig("/mnt/data/bin/ai_model/user_config.json");
       if (cfg_ret != APP_SUCCESS) {
           ANIP_LOGI("AICFGMNG_RestoreConfig skip, ret=0x%x", cfg_ret);
       }
   }
   ```

以上 3、4、5 三步可一键应用移植包补丁（**已验证可应用**）：
```bash
cd /home/mayi/SDK_CV184X_DC100/sophcam
patch -p1 < <移植包>/patch/sophcam_aicfg_integration.patch
```

### 步骤 6：重新构建

SDK 侧（生成新分区表与固件，**必须重新构建 fsbl/uboot**）：
```bash
cd /home/mayi/SDK_CV184X_DC100
source build/envsetup_soc.sh
defconfig cv1842cp_dc100_spinand
build_fsbl && build_uboot    # 重新生成 cvipart.h / fip
build_all
```

sophcam 侧：
```bash
cd /home/mayi/SDK_CV184X_DC100/sophcam
make cv1842cp_dc100_defconfig
make -j && make install      # 或 make pack_data
```

### 步骤 7：把 user_config.json 固化进 AI_CFG 分区

分区初始为全 0xFF（未写状态），需固化一次出厂配置。两种方式：

- **方式 A（推荐，产线/升级用）**：在 sophcam 中调用一次
  `AICFGMNG_UpdateFromFile("/mnt/data/bin/ai_model/user_config.json")`
  （可临时加到 `anip_proc_init` 恢复代码之后，或做成独立小工具；固化后建议保留 Update 接口以便后续更新授权配置）。
- **方式 B（开发验证）**：在设备上手动写一个临时程序调用
  `AICFGMNG_Update(data, len)`，data 为 user_config.json 文件内容。

固化后 `AICFGMNG_Get` 应能原样读出 JSON 内容。

### 步骤 8：验证清单

1. 重启后 `cat /proc/mtd`，应出现一行：
   ```
   mtdX: 00080000 00020000 "AI_CFG"
   ```
   （size=512KB，erasesize=128KB）
2. `dmesg | grep -i mtd` 能看到分区注册。
3. 删除 `/mnt/data/bin/ai_model/user_config.json`，重启动物识别功能，
   日志出现 `AICFGMNG_RestoreConfig ... restored from AI_CFG partition`，文件被恢复。
4. 文件已存在且内容一致时，重启不重复覆盖（日志 `already up-to-date, skip`）。
5. 动物识别服务正常：`kt_ani_init` 返回 `KT_ANI_OK`（日志 `ANIP init ... start` 后无
   `kt_ani_init failed`）。
6. 分区无数据（未固化）时，不影响原有逻辑（`RestoreConfig` 返回 ENODATA 并跳过）。

---

## 5. 移植包文件清单（本会话产物，位于 `/home/mayi/SDK_CV184X_DC309/DC100_AI_CFG_PATCH/`）

| 文件 | 说明 | 状态 |
|---|---|---|
| `caminfo.md` | 本文档 | ✅ |
| `aicfgmng.h` | DC100 新模块头文件 | ✅ 语法已验证 |
| `aicfgmng.c` | DC100 新模块实现（mtd 读写 + CRC32 + 坏块处理） | ✅ 语法已验证 |
| `partition_spinand.xml` | DC100 新版完整分区表（含 AI_CFG 512KB） | ✅ 已用 mkcvipart.py 验证布局 |
| `patch/sophcam_aicfg_integration.patch` | manager/Makefile + app_services/Makefile + anip_service.c 集成补丁 | ✅ 已用 DC100 真实文件副本应用验证 |

---

## 6. 注意事项与风险

1. **分区表修改影响后续所有分区偏移**：ROOTFS/DATA 偏移后移 512KB。
   - 升级/烧录必须**整包烧录**（fsbl/fip/uboot/rootfs/data 一起），否则新旧分区错位；
   - 已存在 `/mnt/data` 数据的设备，DATA 分区偏移变化相当于“换了新分区”，
     旧数据不可见（需重新格式化或先备份）。
2. **mtd 节点序号变化**：AI_CFG 插入后，ROOTFS/DATA 的 `/dev/mtdN` 序号 +1；
   若设备上任何脚本/程序硬编码 mtd 序号，需要同步检查（升级脚本走整包烧录，一般无碍；
   sophcam 侧代码均按分区名查找，不受影响）。
3. **权限**：读写 `/dev/mtdN` 需要 root；sophcam 以 root 运行即可。
4. **坏块**：512KB = 4 个 128KB 擦除块；`aicfgmng` 自动跳过坏块，
   坏块过多导致读不满时 `Get` 返回 `ENODATA`（视为无数据，不阻断业务）。
5. **不要手工改生成文件** `fsbl/build/cvipart.h`、`u-boot-2021.10/include/cvipart.h`，
   一律通过 `partition_spinand.xml` + 重新构建生成。
6. `AICFG_DATA_SIZE_MAX` 为 128KB（分区 512KB 的一半），user_config.json 约 150B，余量充足；
   若将来要存 `run.json`，建议另建分区或扩大该上限（需同步改分区大小，两者要匹配）。
7. `mkcvipart.py` 生成 `cvipart.h` 的偏移按分区表顺序累加，分区表行序不可乱（ENV/ENV_BAK 等
   特殊分区位置由脚本内部逻辑依赖）。
8. 若 DC100 sophcam 的 `kt_ani_init` 目录将来变化，同步修改
   `anip_service.c` 中 `AICFGMNG_RestoreConfig` 的路径参数即可。

---

## 7. 参考代码位置速查

| 内容 | 路径（DC309） |
|---|---|
| 分区表（含 CAM_INFO） | `build/boards/cv184x/cv1842cp_dc309_spinand/partition/partition_spinand.xml` |
| 分区生成脚本 | `build/tools/common/image_tool/mkcvipart.py` |
| 生成的 cvipart.h（示例） | `fsbl/build/cvipart.h`、`u-boot-2021.10/include/cvipart.h` |
| 分区读写模块 | `sophcam_v20260623/applications/manager/src/caminfo/caminfomng.c` |
| 模块头文件 | `sophcam_v20260623/applications/manager/include/caminfomng.h` |
| 模块注册 | `sophcam_v20260623/applications/manager/Makefile` |
| 使用方示例 | `sophcam_v20260623/applications/dashcam/mode/src/photomode.c`、`media/src/media_init.c`、`ui/aicamera/dc309/src/guiguider_ui/auto_calibration.c` |
