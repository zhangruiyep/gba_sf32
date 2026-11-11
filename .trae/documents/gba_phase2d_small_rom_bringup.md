# Phase 2-D / 2-E：容量重新评估与真实 ROM 接入方案

> 状态：**2-D 已链接通过（`main.bin` 16,342,472 B / 15.585 MiB，余 264 KB）。
> 2026-11-11 首烧真机部分验证：TF 卡识别 + `dfs_mount` 成功，无 ROM 时按设计回退「仅 BIOS」且无 fault；
> 待放入 4 MiB `gba.gba` 后验证卡带执行**。
> 上一阶段 2-C 已真机验证通过（BIOS 从 reset vector 跑到 0x348 空闲态，236 个首次入口，屏幕居中 240×160 白块）。
> 本文档是重开任务时的唯一交接依据。

---

## 1. 本轮讨论的最终结论（2026-11-11 修订：ROM 放 TF 卡）

1. **黄山派有 TF 卡槽**（SPI1，板级 `bsp_pinmux.c` 已配好，与 PureDOOM 同一块板），此前「无卡槽」的结论作废。
2. **ROM 本体放 TF 卡，开机整份读入 PSRAM**，`set_rom(psram_ptr, size)`；重编译代码仍全部走 NOR XIP。
3. **否决「直接从 TF 按需读 ROM 数据」**：SPI 12MHz ≈1.2MB/s，每帧仅 ~20KB 预算，512B 随机块 ~0.4ms，而 bus 读是同步 inline 调用，会阻塞 DMA/IRQ/音频。分页读卡只作为 2-E 的可选优化记录，不进当前实施。
4. **`.gba` 仍必需**：重编译只翻译指令，图形/音频/关卡数据运行时仍经 `0x08000000` 读原镜像；解释器兜底要原始字节；`set_rom` 还要扫 matrix/RTC/gyro。
5. **NOR 只需装代码**：12.09(shard) + 1.92(dispatch) + 0.44(symbol) + 0.87(固件) + 0.04(SD/FATFS) ≈ **15.36 MiB vs 15.84 MiB**，余量 ~0.48 MiB，**必须真实链接量测**（12.09 是手工 `-Os` 粗估，不含常量池/对齐/跳转表）。
6. **PSRAM 预算**：machine arena 1MB + 冷缓冲 arena 1MB + LCD 343KB + ROM 4MB ≈ 6.4MB / 8MB。

---

## 2. 已确认的硬件事实

| 项 | 数值 | 来源 |
|---|---|---|
| 片上 SRAM | 512 KB | 芯片手册 |
| OPI-PSRAM | 8 MB @ `0x60000000`，region `PSRAM_DATA` | 板级 `ptab.json` |
| 内置 NOR | 16 MB @ **`0x12000000`**（`flash2` base） | 板级 `ptab.json` |
| NOR `main` 分区 | `offset 0x00020000`, **`max_size 0x00800000`（8 MiB）** | 板级 `ptab.json` L29-44 |
| NOR 扩分区后可用 | `0xFD8000` = 16,613,376 B ≈ **15.84 MiB** | 删 dfu / dfu_download / fs_root 后 |
| TF / SD 卡 | **有**，SPI1：CLK PA28 / MOSI PA24 / MISO PA25 / CS PA29 / CD PA27 | 板级 `bsp_pinmux.c`；SDK `spi_msd.c` 注册 `sd0`（`INIT_PREV_EXPORT` 自动初始化）；PureDOOM 同板实测 |
| TF 读取速度 | SPI 上限 12 MHz ≈ 1.2 MB/s，4 MiB 整份读入约 3.5 s | 板级 SPI1 配置 |
| 面板 | CO5300 390×450 RGB565 | 2-C 已实测 |
| 目标 ROM | Mario Kart Super Circuit (U).gba = **4,194,304 B（4 MiB）** | `roms/` |

---

## 3. 评估不准确的 6 处（已核实）

| # | 原评估 | 实际情况 | 影响 |
|---|---|---|---|
| 1 | NOR 在 `0x00200000` | Flash 映射地址是 **`0x12000000`** | 笔误；`0x00200000` 在 GBA 语义里是 EWRAM，易误读 |
| 2 | NOR 可用 = 16MB − ftab − bootloader = 15.84 MiB | 板级 `main` 分区 `max_size` 仅 **8 MiB** | **严重**：不改分区表，12MB shard 连链接都进不去 |
| 3 | GBA ROM 镜像放 PSRAM，不占 NOR | PSRAM 掉电丢失，ROM 原始数据必须持久存放；但不必占 NOR——板上有 TF 卡槽 | 已解决：4 MiB ROM 放 TF 卡，开机整份读入 PSRAM，NOR 只装代码 |
| 4 | GbaBus / const 表直接放 PSRAM | `link_copy.lds` 的 `.RW_PSRAM_NON_RET` 无 `AT>`，启动拷贝表不含 PSRAM | 运行期只能 memset 后写入；链接期带初值的表不能放 |
| 5 | SRAM 512KB 装得下（100+76.8+320+15） | GbaBus ~386KB + 两帧 ~230KB = 616KB | 2-C 已改 PSRAM placement new，但 plan 内存表未回写 |
| 6 | 「重编译代码 12.68MB」当 NOR 占用 | 只是 8 个 shard 的 `.text` 合计，不含 dispatch 2.01MB / symbol+runtime 0.44MB | 低估约 3.3 MB |

---

## 4. 真实容量账（2026-11-11 实测，已通过）

```
shard .text           12,863,180 B   (8 个 shard，-Oz)
dispatch_table.o       2,567,136 B   (游戏 167,910 条 + native-RAM 27,078 条，12 B/条)
BIOS/runtime/SDK         ~880,000 B   (含 2-C 固件主体、DFS/FATFS、spi_msd、gba_cart_sd.c)
─────────────────────────────────────
main.bin              16,342,472 B = 15.585 MiB
分区上限 0xFD8000      16,613,376 B = 15.84 MiB
余量                     270,904 B (264 KB)
```

第一次链接（带 `symbol_map.cpp` 519,526 B）报 `region ROM overflowed by 248,644 bytes`，
去掉符号表后通过。**结论：ROM 放 TF 卡后，NOR 装得下全量重编译代码，不需要瘦身。**

`symbol_map.cpp` 只是「地址 → 函数名」记录，供串口 trace 使用；不链接后 trace 里函数名
退化为地址，执行路径、调度、native-RAM 模板注册均不受影响。

---

## 5. 优化手段（已采用一条，其余留作后备）

| 手段 | 现状 | 效果 | 是否采用 |
|---|---|---|---|
| 不链接 `symbol_map.cpp` | 519,526 B | 省 519 KB；trace 函数名退化为地址 | ✅ 已采用（链接才通过） |
| dispatch 表按函数去重 + 区间编码 | 2,567,136 B | 省 ~2.2 MB | ⏳ 未用，余量够；2-E 恢复符号名时一并做 |
| shard 关 `-fexceptions -funwind-tables` | `.ARM.exidx` 146,064 B | 省 ~140 KB | ⏳ 未用；生成代码不抛异常，但会让异常穿过 shard 帧时不可回溯 |
| shard 已用 `-Oz` | 12,863,180 B | 已生效 | ✅ 一直如此 |

真实链接只需砍 248,644 B，第一条就够了。

### 若将来仍不够，按此顺序加码

1. **选择性重编译**：`gba_recompile --max-functions N` + config TOML 排除函数。
   缺失函数不会崩——[runtime_arm_default_aborts.cpp L1229-1232](file:///e:/work/Github/gba_sf32/third_party/gbarecomp-main/src/runtime/runtime_arm_default_aborts.cpp#L1229-L1232) 的 `runtime_dispatch_miss` 会桥接到解释器执行整个缺失调用子树。
   风险：一旦进入冷函数，整棵子树解释执行，帧率下降幅度需实测。
2. **换 32 MB NOR 的模组**：硬件决策，软件改动最小。

> 已排除：外挂 flash（无硬件）、代码放 PSRAM 执行（8MB 装不下代码+ROM）、从 TF 卡按需读 ROM 数据（§1 第 3 条）、gc-sections（每个函数都被 dispatch 表引用，gc 不掉）。

---

## 6. 修正后的执行顺序

### M0：完整链接量测 ✅ 已完成（2026-11-11）

结果：`main.bin` = 16,342,472 B（15.585 MiB），对 15.84 MiB 分区余 270,904 B。
第一次带 `symbol_map.cpp` 报 `region ROM overflowed by 248,644 bytes`，去掉后通过。
`main.elf`：text 16,338,480 / data 3,988 / bss 6,829,628。

落地内容：
1. 工程级 `ptab.json`（`main` 扩到 `0xFD8000`，删 dfu / dfu_download / fs_root，kvdb 移到 `0xFF8000` / `0xFFC000`），构建日志已确认 SDK 采用。
2. `runtime_src` 追加 `out/cart_slim/` 的 8 个 shard + `dispatch_table.cpp`（**不含** `symbol_map.cpp`）。
3. 编译命令见 §8。

### M1：链路自检（已被 M2 的真实卡带路径覆盖）

原计划用 32 KB 自造测试 ROM 单独验证链路，现 2-D 直接接真实 ROM，
`gba_cart_load()` 失败时会自动回退到「仅 BIOS」路径，可用于区分卡带问题与链路问题。

### M2：真实 ROM 端到端（TF 卡）

- `$env:GBARECOMP_SLIM="1"; gba_recompile.exe --rom "roms\Mario Kart Super Circuit (U).gba" --out out\cart_slim`
- 把 ROM 拷到 TF 卡根目录并命名 `gba.gba`（FAT32）
- 启动流程（已实现在 [gba_cart_sd.c](file:///e:/work/Github/gba_sf32/src/gba_cart_sd.c) + [gba_device.cpp](file:///e:/work/Github/gba_sf32/src/gba_device.cpp)）：
  1. 轮询 `rt_device_find("sd0")` → `dfs_mount("sd0", "/", "elm", 0, 0)`
  2. `dfs_file_read` 经 16 KiB SRAM bounce buffer 分块拷进 PSRAM（`g_gba_cart_image`）
  3. `set_rom(psram_ptr, size)`，打印 ROM 标题（0xA0）与游戏码（0xAC）
- 无卡 / 无文件 / 大小非法 → 回退 2-C「仅 BIOS」路径并打提示，不会 fault
- 按 M0 结论决定是否加 `--max-functions`、是否改 `-Oz`

---

## 7. 待实测 / 未决项

| # | 事项 | 状态 / 为什么必须实测 |
|---|---|---|
| 1 | 完整链接后的真实占用 | ✅ 已测：`main.bin` 16,342,472 B，text 16,338,480 / data 3,988 |
| 2 | shard 编译选项 | ✅ `-Oz` 已在用（12,863,180 B）；`-fexceptions` 仍开启，未关 |
| 3 | 工程级 `ptab.json` 覆盖是否真的生效 | ✅ 已验证：构建日志打印 `ptab E:\work\Github\gba_sf32\ptab.json` |
| 4 | TF 卡挂载 + 4 MiB 读入耗时 | 挂载 ✅ 已验证：29.29 GB SDHC，`[gba] TF card mounted on /`；读入耗时待放 ROM 后测（SPI1 12 MHz ≈ 1.2 MB/s，估 ~3.5 s） |
| 5 | PSRAM 占用是否在 8MB 以内 | ✅ 链接期 bss 6,829,628 B（含 arena 1MB + 冷缓冲 1MB + LCD 343KB + ROM 4MB） |
| 6 | cart dispatch 是否命中 native-RAM 模板 | 待真机：IWRAM/EWRAM 里的代码靠 `.init_array` 注册的模板；若 `lookup_native_ram` 未命中会走解释器 |
| 7 | 帧率（全静态重编译路径） | 待真机：2-D 首次跑真实卡带，需看是否满 60fps |

---

## 8. 编译命令（PowerShell）

8 个 shard 每个都是 15~20 MB 的单文件 C++，`-j8` 并发编译会耗尽本机内存（`cc1plus` 申请失败）。
正确做法是**先 `-j1` 串行编 shard，再 `-j4` 全量构建**：

```powershell
$env:SIFLI_SDK='E:/work/gitee/sifli/SiFli-SDK/v2.5.1'; $env:SIFLI_SDK_PATH=$env:SIFLI_SDK
$env:RTT_CC='gcc'; $env:RTT_EXEC_PATH='D:\sifli\env\tools\gnu_gcc\arm_gcc_14.2.1\bin'
$env:PYTHONHOME='D:\sifli\env\tools\python-3.11.9-amd64'
$env:PYTHONPATH='D:\sifli\env\tools\python-3.11.9-amd64\Lib;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\build'
$env:Path="$env:PYTHONHOME;$env:PYTHONHOME\Scripts;$env:RTT_EXEC_PATH;$env:Path"
$scons = 'D:\sifli\env\tools\python-3.11.9-amd64\Scripts\scons.exe'

# 1) shard 串行编译（首次约 15 分钟；每个约 70~130 s）
& $scons --board=sf32lb52-lchspi-ulp -j1 out\cart_slim\recompiled_000.o out\cart_slim\recompiled_001.o `
    out\cart_slim\recompiled_002.o out\cart_slim\recompiled_003.o out\cart_slim\recompiled_004.o `
    out\cart_slim\recompiled_005.o out\cart_slim\recompiled_006.o out\cart_slim\recompiled_007.o *> out\build_shards.log

# 2) 全量构建 + 链接（约 1 分钟）
& $scons --board=sf32lb52-lchspi-ulp -j4 *> out\build_2d_full.log
"exit=$LASTEXITCODE"
```

工程根目录的 `ptab.json` 会整套替换板级分区表（构建日志里可确认打印的是 `ptab E:\work\Github\gba_sf32\ptab.json`），
把 `main` 扩到 `0xFD8000`。若日志显示的是板级 `ptab.json` 路径，说明工程级文件没被识别，链接会卡在 8 MiB。

烧录由用户本人执行：`build_sf32lb52-lchspi-ulp_hcpu\uart_download.bat`（串口 COM14）。烧录前把 ROM 拷到 TF 卡根目录命名 `gba.gba`。

---

## 9. 长期约束（不可违反）

- `sifli-sdk` 下的代码**不能改**；临时加 DEBUG 可以，定位完必须全部还原。
- 不手工修改 ROM 派生的生成 C/C++（gbarecomp CLAUDE.md 原则）。
- 只改工程内文件（`src/`、`SConstruct`、`SConscript`、`rtconfig.py`、`Kconfig.proj`、`proj.conf`、工程级 `ptab.json`）。
- scons 链接失败会把 `.o` / `main.elf` 移进回收站，**禁止访问或恢复回收站**。
- README 只保留必要的软件目录说明、编译步骤和注意事项。
- 每阶段完成后更新 `plan/gba_hsp_plan.md`。
