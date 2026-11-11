# Phase 2-D 前置：固件体积实测，再定方案

> 目标板：SF32LB525UC6（`sf32lb52-lchspi-ulp`），16MB NOR / 8MB OPI-PSRAM / TF 卡槽
> 本轮只做**量测**：拿到「把全部 cart 代码 + dispatch table + BIOS 链进固件」的真实体积。
>
> **2026-11-11 修订**：ROM 本体改放 **TF 卡**（开机整份读入 PSRAM），不再占 NOR；
> 因此本文件的判定对象只剩「代码体积」一项，M4 从「ROM 压缩率」改为「TF 读取耗时」。
> 之前「板子无 TF 卡槽 / ROM 必须存 NOR」的结论作废，最新方案见
> [gba_phase2d_small_rom_bringup.md](file:///e:/work/github/gba_sf32/.trae/documents/gba_phase2d_small_rom_bringup.md)。
> **量测已完成**：`main.bin` 16,342,472 B（15.585 MiB），分区余 264 KB，见 §4 账本。
> **首烧结果（2026-11-11）**：TF 卡（29.29 GB SDHC）识别并挂载成功，卡上无 `gba.gba` 时打印
> `not found` 并回退「仅 BIOS」（236 入口、无 fault）；卡带执行待放入 ROM 后验证。

---

## 1. 摘要

上一版计划基于「8 个 shard `.text` 合计 12.68MB」等估算，得出 Mario Kart 放不下的结论。
估算链里有三个不确定点，必须先实测掉：

1. 12.68MB 是手工用 `-Os -fno-exceptions -fno-rtti` 编的；**工程实际用的是 `-fexceptions -funwind-tables`（`SConstruct:26`）**，
   实际 `.text`/`.rodata` 可能显著更大。
2. `dispatch_table.cpp`（167,910 条 × 12B）和 `symbol_map.cpp` 只按源码大小估过，没量过 `.o`。
3. 链接期还有对齐、填充、`.ARM.exidx`、字面量池等隐性开销，只有真链接一次才知道。

所以本轮产出是 **一份体积账本 + 一个由规则自动选出的方案**，不是直接改代码出画面。

**修正一个数字**：16MB NOR 可用空间 = 0x1000000 − 0x20000(ftab+bootloader) − 0x8000(kvdb tail)
= **0xFD8000 = 16,613,376 B ≈ 15.84 MiB**（上一版写的 15.34 MiB 算错了，少算 0.5MB）。

---

## 2. 当前已确认的基线

| 项 | 值 | 来源 |
|----|----|------|
| 当前固件 `main.bin` | 914,188 B（含 BIOS 重编译 + runtime + app + SDK） | `build_sf32lb52-lchspi-ulp_hcpu/main.bin` |
| 8 个 shard 手工编 `-Os` 的 `.text` 合计 | 12,675,432 B | 上一轮实测 |
| `kDispatchTable` 条数 | 167,910 | `out/cart_slim/dispatch_table.cpp:167923` |
| `DispatchEntry` 大小 | 12 B（`u32 addr + u8 thumb + u8 resume + pad2 + fn*`） | `runtime_arm.cpp:37-42` |
| Mario Kart ROM | 4 MiB，放 **TF 卡** `/gba.gba`，开机读入 PSRAM | `roms/Mario Kart Super Circuit (U).gba` |
| TF 卡 | SPI1：CLK PA28 / MOSI PA24 / MISO PA25 / CS PA29 / CD PA27，上限 12 MHz ≈ 1.2 MB/s | 板级 `bsp_pinmux.c`；SDK `spi_msd.c` 注册 `sd0` |
| 可用 flash | **15.84 MiB** | 工程级 `ptab.json`：ftab 0x0/0x8000、bootloader 0x10000/0x10000、main 0x20000/0xFD8000 |
| 8MB PSRAM 现状 | machine ~620KB + LCD ~343KB + 冷缓冲 arena 1MB + ROM 4MB ≈ 6.4MB，余 ~1.6MB | `main.map` |

---

## 3. 量测步骤（4 组，全部只落在工程内文件，不动 sifli-sdk）

### M1 — 目标文件级量测（不改工程结构，只额外编译）

用工程真实编译选项，把 8 个 shard 与 2 个表文件各编一次，量 `.text/.rodata/.data`：

```powershell
# 选项从 build 日志里取现成的一条 CXX 命令行，改成 -c 单文件，逐个产出 .o 到 out\size_probe\
# 关键开关：-std=c++20 -fexceptions -funwind-tables -fno-threadsafe-statics -mcpu=cortex-m33 -mthumb
arm-none-eabi-g++ <工程 CXXFLAGS> -c out\cart_slim\recompiled_000.cpp -o out\size_probe\recompiled_000.o
...
arm-none-eabi-size out\size_probe\*.o
```

产出：每个 shard / `dispatch_table.o` / `symbol_map.o` / `bios_recompiled.o` / `bios_dispatch_table.o` 的精确三段体积，
以及「工程选项 vs 手工 -Os」的差值。

> 若取不到干净的单文件命令行，跳过 M1 直接做 M2 —— M2 的 `nm --size-sort` 同样能归因。

### M2 — 完整固件链接量测（本轮的权威数字）

临时（但也是 2-D 必需）的两处改动：

1. `src/SConscript`：把 `out/cart_slim` 接进 `gba_runtime` group
   ```python
   cart_dir = os.path.join(root, 'out', 'cart_slim')
   cart_src = sorted(glob.glob(os.path.join(cart_dir, 'recompiled_*.cpp')))
   runtime_src += cart_src + [os.path.join(cart_dir, 'dispatch_table.cpp'),
                              os.path.join(cart_dir, 'symbol_map.cpp')]
   runtime_cpppath.append(cart_dir)
   ```
2. `src/gba_device_stubs.cpp`：删掉 `kDispatchTable` / `kDispatchTableLen` 空桩
   （[L73-L87](file:///e:/work/Github/gba_sf32/src/gba_device_stubs.cpp#L73-L87)），否则与真实表重复定义。

`src/gba_device.cpp` **本轮不动**（仍 `set_rom(nullptr,0)`、BIOS 进 cart 即 break），
所以固件能和 2-C 一样跑，只是多了 15MB 代码在 flash 里。

编译后采集：

```powershell
arm-none-eabi-size build_sf32lb52-lchspi-ulp_hcpu\main.elf     # text / data / bss
(Get-Item build_sf32lb52-lchspi-ulp_hcpu\main.bin).Length      # 真正烧进 flash 的字节数
arm-none-eabi-nm --size-sort -S build_sf32lb52-lchspi-ulp_hcpu\main.elf | Select-Object -Last 30
```

产出：`S_code = main.bin 字节数`（这是**完整固件**，已含 SDK+runtime+BIOS+全部 cart）。

> 风险：8 个 shard 共约 150MB C++，全量编译 + 15MB 链接耗时较长、占内存；若链接把 `.o` 移进回收站，
> 按既有约定**不去访问或恢复回收站**，直接重编。

### M3 — 瘦身档位量测（在 M2 基线上做 3 组增量）

| 档位 | 做法 | 量什么 |
|------|------|--------|
| M3a `gc-sections` | `SConstruct` 的 `rtconfig.CXXFLAGS` 加 `-ffunction-sections -fdata-sections`，链接加 `--gc-sections` | `main.bin` 降幅 |
| M3b 关 unwind 表 | 只对 cart shard 单独编（M1 的方式）加 `-fno-unwind-tables -fno-asynchronous-unwind-tables` | shard 0 的 `.text + .ARM.exidx` 降幅 × 8 |
| M3c dispatch 压缩 | 纯计算：167,910 × (12−8) = **671,640 B**，另加 167,910×2B 的函数索引表（可与表合并，取 0 额外开销） | 理论值 |

M3a 只量链接体积；**不在本轮验证 `gc-sections` 是否影响 SDK 启动**（如果最终采用，2-D 实施时再跑真机）。

### M4 — TF 卡读取量测（真机）

| 项 | 做法 | 量什么 |
|----|------|--------|
| M4a 挂载 | `rt_device_find("sd0")` + `dfs_mount("sd0","/","elm",0,0)` | 打印挂载成功耗时 |
| M4b 整份读入 | 4 MiB `/gba.gba` 经 16 KiB SRAM bounce buffer 读进 PSRAM | 打印起止时间，折算 MB/s |

预期：SPI 12 MHz 上限约 1.2 MB/s，4 MiB 约 3.5 s。这个时间只在开机出现一次，不影响帧率。
不再考虑「ROM 压缩存 NOR」（ROM 已不占 NOR）；也不做按需读卡——bus 读是同步 inline 调用，
512B 随机块 ~0.4ms 会阻塞 DMA/IRQ/音频，而每帧预算只有 ~20KB。

---

## 4. 体积账本与判定规则（2026-11-11 已实测填表）

| 项 | 字节 | 说明 |
|----|------|------|
| A. 完整固件（含全部 cart 代码 + `symbol_map.cpp`） | 16,862,020 | 链接失败：`overflowed by 248,644` |
| B. A − `symbol_map.cpp` | **16,342,472** | ✅ 实际交付的 `main.bin`（15.585 MiB） |
| C. 8 个 shard `.text` 合计 | 12,863,180 | `-Oz` |
| D. `dispatch_table.o` | 2,567,136 | 游戏 167,910 + native-RAM 27,078 条 × 12 B |
| E. 可用 flash（`0xFD8000`） | 16,613,376 | 15.84 MiB |
| F. 余量（E − B） | 270,904 | 264 KB |
| G. `main.elf` text / data / bss | 16,338,480 / 3,988 / 6,829,628 | bss 6.51 MB，PSRAM 8 MB 内 |

判定结果：命中规则 1（`B ≤ E`），**已走 TF 卡方案，无需瘦身**。
唯一取舍是不链接 `symbol_map.o`（519,526 B）：trace 里函数名变地址，执行不受影响。

备用量测（未使用，留作 2-E）：M3a/M3b（shard 去 unwind、dispatch 表去重）、M3c（`--max-functions` 截断 671,640 B）。

ROM 存哪里已不再是变量：TF 卡方案在不改执行模型、不影响帧率的前提下彻底移除了 4 MiB 占用。

---

## 5. 验证与交付

- 每步产物：`out/size_probe/*.o`、`build_sf32lb52-lchspi-ulp_hcpu/main.elf|main.bin`、`arm-none-eabi-size` / `nm` 输出。
- 交付物：第 4 节的账本（填真实数字）+ 由规则选出的方案 + 与方案对应的下一步实施清单。
- 结论回写 `plan/gba_hsp_plan.md`（2-D 条目的状态与体积数据）。
- 改动可回滚：M2 的两处改动就是 2-D 的必需改动，保留；`SConstruct` 的 M3a 开关默认关闭，量完即还原。

---

## 6. 附录：2-D 已落地的接线（TF 卡方案）

1. `proj.conf`：`CONFIG_BSP_USING_SPI1` + SPI1 TX/RX DMA + `CONFIG_RT_USING_SPI_MSD` + `CONFIG_RT_USING_DFS_ELMFAT`。
2. `src/gba_cart_sd.c`（新增）：轮询 `sd0` → `dfs_mount("sd0","/","elm",0,0)`；用 L1 SRAM 16 KiB bounce buffer
   分块 `dfs_file_read` 再 `memcpy` 到 PSRAM（`L2_NON_RET_BSS` 的 4 MiB `g_gba_cart_image`）。
   走 bounce buffer 是因为 SPI MSD 的 DMA 目标若落在可缓存 PSRAM 会留陈旧 cache line。
3. `src/SConscript`：`out/cart_slim` 的 8 个 shard + `dispatch_table.cpp` + `symbol_map.cpp` 接进 `gba_runtime`。
4. `src/gba_device_stubs.cpp`：删掉 `kDispatchTable` / `kDispatchTableLen` 空桩，避免与真实表重复定义。
5. `src/gba_device.cpp`：手工遍历 `__init_array_start/end` 执行静态构造器（SDK 的 `__libc_init_array` 是空桩，
   而 `dispatch_table.cpp` 末尾的 `register_native_ram` 依赖它）；`gba_cart_load("/gba.gba")` 成功后
   `set_rom(psram_ptr, size)`；主循环只在无卡时 `pc >= kCartBase` 退出。
6. `runtime_arm.cpp` 的判据是 `pc < 0x4000 → BIOS 表，否则游戏表`，接上真实表即可分派 cart。
7. present 钩子按 CPU 模式（`0x10/0x1F`）而非显示模式放行，mode 3 也能出帧
   （[runtime_bus_bridge.cpp:1206-1227](file:///e:/work/Github/gba_sf32/third_party/gbarecomp-main/src/runtime/runtime_bus_bridge.cpp#L1206-L1227)）。
