# 打包镜像 bootlogo 与 NAND ID 补丁改动记录

> 记录日期：2026-09-05
> 涉及代码库：`u-boot-2021.10`、`linux_5.10`、`build/`
> 板卡：`cv1842cp_dc309_spinand`（CHIP=cv1842cp，BOARD=dc309_spinand）

---

## 概述

本次会话共两类改动：

| # | 改动 | 涉及文件 |
|---|------|----------|
| 1 | 应用 NAND ID 补丁（uboot + linux） | `u-boot-2021.10/.../cvsnfc_spi_ids.c`<br>`linux_5.10/.../cvsnfc_spi_ids.c` |
| 2 | 改造 `pack_prog_img` 的 bootlogo 打包逻辑 | `build/common_functions.sh`<br>`build/tools/common/image_tool/cimg2raw.py`（新增） |

---

## 一、NAND ID 补丁应用

### 1.1 补丁来源

| 补丁文件 | 目标代码库 | 目标文件 |
|----------|-----------|----------|
| `uboot.diff/54b367e.diff` | `u-boot-2021.10` | `drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c` |
| `linux.diff/e6b42f5.diff` | `linux_5.10` | `drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c` |

两个补丁同源（JIRA: CV184XSDK-1292，作者 xiaofei.liu，2026-07-15），均新增 4 个 GD NAND ID：

| 名称 | ID | id_len | pagesize | oobsize |
|------|-----|--------|----------|---------|
| `GD5F1GQ4xExxH` | `0xc8, 0xd9` | 2 | 2K | 64 |
| `GD5F1GQ4U` | `0xc8, 0xa1, 0x48` | 3 | 2K | 128 |
| `GD5F1GQ4U` | `0xc8, 0xb1, 0x48` | 3 | 2K | 128 |
| `GD5F1GQ4U` | `0xc8, 0xc1` | 2 | 2K | 128 |

### 1.2 应用命令

```bash
# 试运行检查（建议先做）
cd u-boot-2021.10 && git apply --check /home/mayi/SDK_CV184X_DC309/uboot.diff/54b367e.diff
cd linux_5.10     && git apply --check /home/mayi/SDK_CV184X_DC309/linux.diff/e6b42f5.diff

# 正式应用
cd u-boot-2021.10 && git apply /home/mayi/SDK_CV184X_DC309/uboot.diff/54b367e.diff
cd linux_5.10     && git apply /home/mayi/SDK_CV184X_DC309/linux.diff/e6b42f5.diff
```

### 1.3 应用结果

两个补丁均 `Applied patch ... cleanly`，但存在行号偏移（源码版本与补丁基准不完全一致，无冲突）：

- uboot：`Hunk #1 succeeded at 434 (offset -31 lines)`
- linux：`Hunk #1 succeeded at 676 (offset -64 lines)`

验证方式（应能搜到新增 ID）：

```bash
grep -n "0xc8, 0xd9" u-boot-2021.10/drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c
grep -n "0xc8, 0xd9" linux_5.10/drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c
```

注意：改动目前是**未提交的工作区修改**，`git status` 可见。

---

## 二、bootlogo 打包逻辑改造（重点）

### 2.1 需求背景

原厂要求：**打包进 `prog_img` 的 logo 必须是未被 `copy_tools` 处理过的原始文件**。

代码层面的佐证（`build/tools/common/image_tool/pack_images.py`）：

```python
def raw_image_check(f, filename):
    if filename == "fip.bin":
        return
    head = f.read(4)
    if head == b"CIMG":
        logging.info("%s is not raw image, please use raw image" % filename)
        sys.exit(-1)
```

即：分区文件若带 `CIMG` 头，生成 `Data.bin` 时会直接报错退出。

### 2.2 关键原理：copy_tools 如何"污染" logo

`build/common_functions.sh` 的 `copy_tools`：

```bash
if [ "$PACK_BOOTLOGO" = "y" ]; then
  python3 "$IMGTOOL_PATH"/raw2cimg.py "$BOOTLOGO_PATH" "$OUTPUT_DIR" "$FLASH_PARTITION_XML"
fi
```

`raw2cimg.py` 依据 partition xml 中 MISC 分区定义：

```xml
<partition label="MISC" size_in_kb="640" readonly="false" file="logo.jpg" mountpoint="" type=""/>
```

把原始 jpg 加上 CIMG 头后，**输出到并覆盖** `$OUTPUT_DIR/logo.jpg`。

路径关系（依据 `build/envsetup_soc.sh`）：

```bash
PROJECT_FULLNAME="$CHIP"_"$BOARD"            # = cv1842cp_dc309_spinand
INSTALL_PATH="$TOP_DIR"/install              # = /home/mayi/SDK_CV184X_DC309/install
OUTPUT_DIR="$INSTALL_PATH"/soc_"$PROJECT_FULLNAME"
```

> **重点**：`$OUTPUT_DIR` 就是 install 下的板卡目录
> = `/home/mayi/SDK_CV184X_DC309/install/soc_cv1842cp_dc309_spinand`

数据流：

```
BOOTLOGO_PATH (原始 raw jpg)
    │  copy_tools → raw2cimg.py
    ▼
$OUTPUT_DIR/logo.jpg (CIMG 包装版，供 usb_dl / OTA 用)
    │  pack_prog_img → cimg2raw.py（本次新增，复原）
    ▼
tmp_dir/logo.jpg (原始 raw jpg) → prog_img.tar.gz
```

### 2.3 CIMG 容器格式（cimg2raw.py 的逆向依据）

**主头 64 字节：**

| 偏移 | 长度 | 含义 |
|------|------|------|
| 0 | 4 | 魔数 `CIMG` |
| 4 | 4 | Version = 1 |
| 8 | 4 | chunk 头大小 = 64 |
| 12 | 4 | chunk 总数 |
| 16 | 8 | 文件总大小 low/high（含所有 chunk 头） |
| 24 | 32 | label（ASCII，`\0` 填充，如 `MISC`） |
| 56 | 8 | 保留 |

**每个 chunk：64 字节头 + 数据**

| 偏移 | 长度 | 含义 |
|------|------|------|
| 0 | 4 | chunk type |
| 4 | 4 | **数据长度**（复原时按此取值） |
| 8 | 8 | 烧录偏移 low/high |
| 16 | 8 | 分区大小 low/high |
| 24 | 4 | CRC32（对小端数据） |
| 28 | 36 | 保留 |

文件小于 16MB 时只有 1 个 chunk，故：**原始大小 = CIMG 文件大小 − 128**（主头 64 + chunk 头 64）。

### 2.4 最终代码改动

`build/common_functions.sh`，`pack_prog_img` 函数 spinand 分支（约 285-292 行）：

```bash
    cp "${OUTPUT_DIR}/rawimages/"* "${tmp_dir}/"
    # logo.jpg in the install board folder was processed by copy_tools
    # (raw2cimg.py adds a CIMG header for usb_dl/OTA). prog_img needs the
    # ORIGINAL file that was NOT processed, so strip the CIMG header back.
    # If it carries no CIMG header, cimg2raw.py copies it as-is.
    if [ -f "$OUTPUT_DIR/logo.jpg" ]; then
      python3 "$IMGTOOL_PATH"/cimg2raw.py "$OUTPUT_DIR/logo.jpg" "${tmp_dir}/logo.jpg"
    fi
```

改动前（原始逻辑）：

```bash
    if [ -n "$BOOTLOGO_PATH" ] && [ -f "$BOOTLOGO_PATH" ]; then
      cp "$BOOTLOGO_PATH" "${tmp_dir}/logo.jpg"
    fi
```

### 2.5 新增工具 `cimg2raw.py`

路径：`build/tools/common/image_tool/cimg2raw.py`（与 `raw2cimg.py` 同目录，`IMGTOOL_PATH` 指向此处）

功能：剥离 CIMG 头还原原始文件。特性：

- 检测魔数，无 `CIMG` 头则**原样拷贝**（幂等，不会二次破坏）
- 逐 chunk 校验 **CRC32**，不匹配则报错（能发现文件损坏或格式误判）
- 用主头的 size 字段交叉校验，不一致仅 warning
- 支持输出参数为目录（沿用源文件名）
- 不 import 任何本地模块，不被其他模块 import

用法：

```bash
python3 build/tools/common/image_tool/cimg2raw.py <CIMG文件> <输出文件|输出目录> [-v]
```

### 2.6 方案演进（走过的弯路，避免重复踩）

| 版本 | 做法 | 问题 |
|------|------|------|
| 原始 | `cp $BOOTLOGO_PATH` | 用的是 bootlogo 下的图，不是 install 板卡目录里的 |
| 尝试 1 | `cp $OUTPUT_DIR/logo.jpg` | ❌ 打进去的是 **CIMG 包装版**，原厂明确不允许；若生成 Data.bin 会被 `raw_image_check` 拒绝 |
| **最终** | `cimg2raw.py $OUTPUT_DIR/logo.jpg` | ✅ 复原成 raw jpg 后打包 |

---

## 三、验证结果

### 3.1 复原功能验证

```bash
python3 build/tools/common/image_tool/cimg2raw.py \
  install/soc_cv1842cp_dc309_spinand/logo.jpg /tmp/out.jpg -v
```

输出：

```
DEBUG: CIMG v1 chunk_hdr_sz=64 total_chunks=1 total_filesz=18888 label=MISC
INFO: Restored ... (18952 -> 18824 bytes)
```

| 检查项 | 结果 |
|--------|------|
| 大小 | 18952 → 18824 字节 |
| 头部 | `ffd8fffe`（JPEG，**非 CIMG**） |
| CRC32 | chunk 校验通过 |
| 文件类型 | JPEG 480x640（与 DC309 屏幕分辨率一致） |
| 幂等性 | raw 输入再跑一次 → 原样拷贝 |

### 3.2 两个 logo 文件的对照（排查用）

| 来源 | 大小 | md5 | 内容 |
|------|------|-----|------|
| `build/boards/cv184x/cv1842cp_dc309_spinand/bootlogo/logo.jpg` | 26931 | `6bc3a2e98e1ed9a64c52776bd7ec7994` | tamrac 商标图 |
| 复原 `install/soc_cv1842cp_dc309_spinand/logo.jpg` | 18824 | `015d273ee2d7c5573ff139715282dfb2` | 星空 WELCOME 图（预期打包的） |

打包后确认方法：

```bash
tar -xzf install/soc_cv1842cp_dc309_spinand/prog_img.tar.gz -O ./logo.jpg | md5sum
# 期望：015d273ee2d7c5573ff139715282dfb2  （星空图）
# 若为 6bc3a2e98e1ed9a64c52776bd7ec7994 说明打的是 bootlogo 的旧图
```

---

## 四、踩坑记录（重要）

### 坑 1：改了 `common_functions.sh` 却不生效

`common_functions.sh` 是被 `source` 进 shell 的，**bash 函数定义在 source 时就驻留内存**。改完文件后，当前终端里的 `pack_prog_img` 仍是旧版本。

**必须先重新 source 再打包：**

```bash
source build/envsetup_soc.sh          # 按平时的方式加载环境
pack_prog_img 0x95c8                  # nandid 换成实际值
```

典型症状：代码已改，但打出来的 logo 仍是旧的（如 tamrac 图）。

### 坑 2：`copy_tools` 会覆盖 install 下的 logo

`pack_flow`（`envsetup_soc.sh` 第 937-945 行）中 `copy_tools` 在打包流程里执行，会用 `BOOTLOGO_PATH` 重新生成并覆盖 `install/soc_xxx/logo.jpg`。

因此：

- **只跑 `pack_prog_img`**（不跑 copy_tools / 完整 build）→ 打包的是 install 下**当前**那份 logo
- **先跑完整 build**（含 copy_tools）→ install 下的 logo 已被 `bootlogo/logo.jpg` 覆盖，复原后内容自然等于 bootlogo 那份

想换开机 logo，最省事的做法是改：

```
build/boards/cv184x/cv1842cp_dc309_spinand/bootlogo/logo.jpg
```

copy_tools 会同步到 install，pack_prog_img 再复原成 raw 打包，两头都对。

> 注意：dc309 板卡原先**没有** `bootlogo/` 目录（只有 `cv1842cp_sm3_81_spinand/bootlogo/` 有），
> 缺失时 `BOOTLOGO_PATH` 会回退到 `build/tools/common/bootlogo/logo.jpg`（通用图，10251 字节）。
> 现已补上 dc309 的 `bootlogo/logo.jpg`。

### 坑 3：OTA 的 `cimg2raw` 与本次新增脚本无关

存在两个同名不同物：

| 文件 | 类型 | 用途 | 运行环境 |
|------|------|------|----------|
| `build/tools/common/ota_tool/utils/cimg2raw` | ARM 32 位 ELF 二进制（1.05MB，Jan 14 2026） | OTA 升级解包 | **设备端** |
| `build/tools/common/image_tool/cimg2raw.py` | Python3 脚本（4KB，本次新增） | 打包时复原 logo | **主机端** |

`pack_upgrade` 和 `ota_tool/gen_ota_script` 引用的是前者，**不受本次改动影响，OTA 功能正常**。

### 坑 4：`rawimages/` 下没有 logo.jpg

`install/soc_cv1842cp_dc309_spinand/rawimages/` 只有 boot/data/fip/rootfs/yoc 等文件，**不含 logo.jpg**，
所以 logo 只能从 `$OUTPUT_DIR/logo.jpg` 取（即本次复原的来源）。

另外 `pack_prog_img` 调用 `pack_images.py` 时带 `-p` 参数，只打印分区表、不生成 `Data.bin`，
因此不会覆盖 `tmp_dir/logo.jpg`。

---

## 五、回退方法

```bash
# 1. 删除新增工具
rm build/tools/common/image_tool/cimg2raw.py

# 2. 恢复打包逻辑
```

把 `common_functions.sh` 的该段改回：

```bash
    if [ -n "$BOOTLOGO_PATH" ] && [ -f "$BOOTLOGO_PATH" ]; then
      cp "$BOOTLOGO_PATH" "${tmp_dir}/logo.jpg"
    fi
```

两步必须一起做，否则 `pack_prog_img` 会因找不到 `cimg2raw.py` 报错。

NAND ID 补丁回退：

```bash
cd u-boot-2021.10 && git apply -R /home/mayi/SDK_CV184X_DC309/uboot.diff/54b367e.diff
cd linux_5.10     && git apply -R /home/mayi/SDK_CV184X_DC309/linux.diff/e6b42f5.diff
```

---

## 六、改动文件清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `build/common_functions.sh` | 修改 | `pack_prog_img` 的 logo 打包改为复原后打包 |
| `build/tools/common/image_tool/cimg2raw.py` | **新增** | CIMG 头剥离工具 |
| `u-boot-2021.10/drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c` | 补丁 | 新增 4 个 GD NAND ID |
| `linux_5.10/drivers/mtd/nand/raw/cvitek/cvsnfc_spi_ids.c` | 补丁 | 新增 4 个 GD NAND ID |

原始文件时间戳参考：`build/tools/common/image_tool/` 下 SDK 自带文件均为 `Jan 14 2026`，
`cimg2raw.py` 为 `Sep 4 11:36`（新增），可据此区分是否为原生文件。
