# GBA ROM 重编译到思澈黄山派 实施计划

> 最后更新：2026-11-11

## 0. 当前状态总览

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 0 | 环境 + LCD 点亮（SDK example/lcd_fb 验证） | ✅ 完成 |
| Phase 1 | PC 端 gbarecomp 重编译 ROM/BIOS → C++20 产物 | ✅ 完成（Mario Kart SC 4MB；8 shard；slim 后 12.68MB `.text`） |
| Phase 2-A | 仓库根独立 scons 工程骨架 | ✅ 编译通过（`main.bin` 325,240 B） |
| Phase 2-B | 显示后端：中央 240×160 RGB565 | ✅ 完成（当时的「滚动彩条」实为 165×15 坐标 bug，已在 2-C 修复） |
| Phase 2-C | runtime 移植到 SiFli HAL | ✅ 完成并真机验证（BIOS 从 reset vector 跑到空闲态，236 个函数入口，无 fault；修复送屏几何后居中 240×160 白块，2026-11-11 复烧确认） |
| Phase 2-D | 真实 ROM 接入：TF 卡读入 PSRAM + cart dispatch 联动 | 🚧 编译链接通过（`main.bin` 16,342,472 B / 15.585 MiB，余 264 KB）；首烧真机已验证 TF 挂载成功、无卡回退正常，待放入 `gba.gba` 验证卡带执行 |
| Phase 2-E | 容量优化（E1 寄存器提升 / E2 派发表压缩 / E3 模板表去重） | 📋 候选已归档（见 §3.2.4）。2-D 未超限，非阻塞；待 TF 卡真实 ROM 验证通过后再决定是否实施 |
| Phase 3 | 音频 / 按键 / 存档 | ⏳ 未开始 |

**烧录已打通**：`sftool 0.2.5`（`C:\Users\zhang\.sifli\tools\sftool\0.2.5`）+ `build_.../uart_download.bat`，串口 COM14（CH340），三镜像 `ftab@0x12000000` / `bootloader@0x12010000` / `main@0x12020000` 写入成功，屏幕居中 240×160 区域出画面。

> **约定：后续下载烧录由用户本人执行**，AI 只负责编译并给出烧录命令。

**Phase 2-D 首烧结果（2026-11-11，COM14）**：SD 卡识别为 29.29 GB SDHC（SPI1 / `spi_msd` 注册 `sd0`），
`dfs_mount("sd0","/","elm",0,0)` 返回成功；因 TF 卡上尚未放 ROM，打印 `/gba.gba not found` 后
按设计回退「仅 BIOS」路径，BIOS 从 reset vector 跑到 `00000348` 空闲态，236 个首次入口，无 fault。
剩余待验证项只有「放上 4 MiB `gba.gba` 后的卡带执行」：ROM 标题/游戏码、4 MiB 读取耗时、出帧与帧率。

> **当前唯一的下一步（阻塞在用户侧）**：往 TF 卡根目录放 `gba.gba`（4,194,304 B，FAT32）后重新上电，
> 复刻首烧串口日志。在这一步拿到真实游戏出画面的结果之前，不启动 Phase 2-E 的任何代码改动。

---

## 1. 硬件确认（锁定）

### 1.1 黄山派开发板

| 项目 | 值 | 来源 |
|------|-----|------|
| 芯片 | **SF32LB525UC6** | 合封 8MB OPI-PSRAM + 16MB NOR Flash |
| 大核 | ARM Cortex-M33 @ **240MHz** | 370 DMIPS |
| 片上 SRAM | **512KB** | HCPU 专用 |
| 屏幕 | CO5300AF-01 AMOLED, **390×450**, QSPI RGB565 | CO5300AF-01 |
| 音频 | AU_DAC1P/N_OUT + AW8155 Class-D PA, PA_EN=PA42 | board.conf |
| KEY1 | PA34 | board.conf |
| KEY2 | PA43 | board.conf |
| Touch | FT6146-M00, I2C | — |
| SDK 路径 | `e:\work\gitee\sifli\SiFli-SDK\v2.5.1` | 用户本地 |

### 1.2 SDK Board 配置锁定

- **Board 名称**: `sf32lb52-lchspi-ulp`（即"立创黄山派"拼音缩写）
- **rtconfig.py**: `CHIP = 'SF32LB52X'`, `JLINK_DEVICE = 'SF32LB52X_NOR'`, `CORE = "HCPU"`
- **关键 board.conf 项**（已预配置好，直接用）：
  ```
  CONFIG_BSP_USING_LCD=y
  CONFIG_LCD_USING_TFT_CO5300=y
  CONFIG_BSP_USING_TOUCHD=y
  CONFIG_BSP_ENABLE_AUD_CODEC=y
  CONFIG_BSP_ENABLE_AUD_PRC=y
  CONFIG_BSP_USING_GPIO=y
  CONFIG_BSP_USING_TIM=y
  CONFIG_BSP_USING_PWM=y
  CONFIG_BSP_USING_KEY1=y / CONFIG_BSP_KEY1_PIN=34
  CONFIG_BSP_USING_KEY2=y / CONFIG_BSP_KEY2_PIN=43
  CONFIG_PA_USING_AW8155=y / CONFIG_AW8155_GPIO_PIN=42
  ```

### 1.3 内存布局

```
片上 SRAM 512KB (0x20000000):
├── rt-thread + SDK 栈堆        ~100KB
├── 2-C 固件实测占用            133,980 B
└── 结论：装不下 GbaBus(~386KB)+两帧(~230KB)=616KB → 已改 PSRAM placement new

OPI-PSRAM 8MB (0x60000000):
├── g_gba_arena（GbaBus + GbaPpu）          1MB 静态预留（实际需 ~620KB）
├── gba_psram 冷缓冲 arena（bump 分配）     1MB 预留（音频采集环已缩到 ~14KB）
├── LCD 全屏帧存 390×450×2                  ~343KB
├── GBA ROM 运行期镜像                      4MB（开机从 TF 卡整份读入）
└── 合计 ~6.4MB / 8MB，余 ~1.6MB

NOR Flash 16MB (0x12000000 = flash2 base):
├── ftab 0x00000000 (32KB) + bootloader 0x00010000 (64KB)
├── main 分区：板级 ptab.json 的 max_size 只有 8MiB（硬上限）
│   └── 工程级 ptab.json 已扩到 0xFD8000 ≈ 15.84MiB
│       （删掉 dfu / dfu_download / fs_root，kvdb 移到 0xFF8000）
└── kvdb 0x00FF8000 (16KB) + 0x00FFC000 (16KB)

TF 卡（SPI1，板级 bsp_pinmux 已配好）:
├── CLK PA28 / MOSI PA24 / MISO PA25 / CS PA29 / CD PA27
├── SDK spi_msd 注册 sd0（INIT_PREV_EXPORT 自动初始化），SPI 上限 12MHz
└── ROM 文件固定在卡根目录 `/gba.gba`

容量结论（2026-11-11 实测，ROM 移到 TF 卡）:
  ROM 本体不占 NOR，只需装重编译代码。真实链接结果：
  8 shard .text 12,863,180 + dispatch 2,567,136 + BIOS/runtime/SDK ~880,000
  = main.bin 16,342,472 B (15.585 MiB)  vs  分区 16,613,376 B (15.84 MiB)
  余量 270,904 B（264 KB）——已通过，无需瘦身。
  代价：symbol_map.cpp（519,526 B）不链接，串口 trace 只显示地址不显示函数名；
  若 2-E 需要恢复函数名，用压缩符号表（区间编码）把 519 KB 压到 ~120 KB。
  构建命令见 .trae/documents/gba_phase2d_small_rom_bringup.md §8。
```

> 注：PSRAM 只能承载运行期数据（BSS + 自己清零/拷贝），**不能**放链接期带初值的 const 表——`link_copy.lds` 的 `.RW_PSRAM_NON_RET` 无 `AT>`，启动拷贝表不含 PSRAM。
> ROM 原始字节必须常驻 RAM（重编译只翻译指令，图形/音频/关卡数据运行时仍经 0x08000000 读取），因此开机整份读入 PSRAM，而不是按需读卡（SPI 12MHz ≈1.2MB/s，每帧仅 ~20KB 预算，同步 bus 读会阻塞 DMA/IRQ）。

---

## 2. 技术路线

```
┌─ 开发主机 (Windows) ──────────────────────────────────────┐
│                                                           │
│  GBA ROM ──▶ gbarecomp (Rust) ──▶ 重编译后的 C 工程        │
│                                                           │
└────────────────────────────────┬──────────────────────────┘
                                 │ arm-none-eabi-gcc / scons
                                 ▼
                          固件镜像 (.bin)
                                 │ USB/UART 烧录
                                 ▼
┌─ 思澈黄山派 (sf32lb52-lchspi-ulp) ──────────────────────────┐
│                                                             │
│  Cortex-M33 @ 240MHz (HCPU, RT-Thread)                      │
│                                                             │
│  ┌────────────────┐   ┌──────────────────┐                │
│  │ gbarecomp 产物  │   │ GBA Runtime (C)    │                │
│  │ 游戏 C 函数     │   │ cpu/ppu/apu/dma   │                │
│  └───────┬────────┘   └────────┬─────────┘                │
│          └──────────┬───────────┘                          │
│                     ▼                                       │
│            ┌────────────────┐                              │
│            │  适配层          │                              │
│            │  render  audio  │                              │
│            │  input  storage │                              │
│            └──┬──────┬───┬──┘                              │
│               │      │   │                                  │
│               ▼      ▼   ▼                                  │
│         LCDC(QSPI)  AUDCODEC  GPIO                         │
│         drv_lcd_fb  DMA/IT   KEY1/KEY2                     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 2.1 GBA 静态重编译生态盘点

#### 2.1.1 路线排除说明（已讨论，不进入实施）

| 路线 | 原理 | 排除原因 |
|------|------|---------|
| **B. gpSP 动态 JIT** | 运行时 JIT 翻译 GBA 指令 → host 指令 | ❌ gpSP 无 Cortex-M 系列 JIT backend；需从零写 Thumb-2 JIT，工作量 20-30 天 |
| **C. 纯解释器（mGBA）** | 逐条解释执行 GBA 指令 | ❌ 240MHz Cortex-M33 跑 ARM7TDMI 解释器 ≈ 刚好在帧率临界线；性能无优化空间 |
| **D. 反编译源码重编译** | 用人反编译好的 C 源码直接编译 | ❌ 只有极少数游戏有完整反编译（Minish Cap/Pokemon Gen3）；反编译项目本身是数月级的多人协作项目 |

#### 2.1.2 静态重编译（A 路线）现有项目

| 项目 | 目标架构 | 产物 | 成熟度 | 和 GBA ARM7TDMI 的关系 |
|------|---------|------|--------|----------------------|
| **gbarecomp** (mstan) | ARM7TDMI | ✅ **C/C++ 代码** | ⭐⭐⭐ 早期实验性，但已跑通 6 个游戏 | **唯一公开的 GBA ARM7TDMI → C 静态重编译工具** |
| N64Recomp | N64 MIPS R4300i | C++ 代码 | ⭐⭐⭐⭐⭐ 成熟（Zelda64Recomp 多个游戏完整可玩） | ❌ 架构不同。核心技术可参考（CFG 分析、硬件 faithful runtime） |
| PS1Recomp | PS1 MIPS R3000A | C++ 代码 | ⭐⭐⭐⭐ 成熟（多个游戏完整可玩） | ❌ 架构不同，同一作者（mstan），gbarecomp 是这套技术栈延续 |
| PS2Recomp | PS2 MIPS R5900 | C++ 代码 | ⭐⭐ CPU 能跑，GPU stub | ❌ 架构不同 |
| GB Recompiled | DMG Z80 | C 代码 | ⭐⭐⭐ 98.9% ROM 可重编译 | ❌ 目标是 8-bit Z80，不是 ARM7TDMI。但技术路线相同，runtime 设计可参考 |
| romdev | 多平台（含 GBA） | wasmcart (WebAssembly) | ⭐⭐⭐⭐ 工具链完整 | ❌ 输出 Wasm 不是 C，目标平台是浏览器不是嵌入式 |
| JR (已归档) | GBA ARM7TDMI | 待定 | ❌ 项目退休 | 作者把资源让给了 mstan 去做 gbarecomp |

#### 2.1.3 为什么只有 gbarecomp 公开可用

GBA ARM7TDMI 和所有其他被静态重编译的目标（N64/PS1/PS2 的 MIPS、DMG 的 Z80）相比，有一个**独有的技术难点**：**ARM/Thumb 双指令集 interworking**。

- GBA 运行时在 32-bit ARM 状态和 16-bit Thumb 状态之间频繁切换（BX/BLX 指令）
- 静态分析需要：准确识别每个函数入口是 ARM 还是 Thumb、正确处理状态切换跳转、CFG 图每个节点都记录期望进入的 CPU 状态
- N64/PS1/PS2 是单指令集 MIPS，没有这个问题

**只有 gbarecomp 公开声称解决了这个问题**，并实际跑通了 6 个游戏。

---

### 2.2 选定引擎：gbarecomp

- **仓库**: https://github.com/mstan/gbarecomp
- **产物**: GBA ARM7TDMI 机器码 → **C 代码**（README 原文："translates ARM7TDMI ARM and Thumb machine code into C++"；但 FireRedLeafGreenRecomp 仓库写 "translated to native C"）
- **已验证游戏**: Pokemon Ruby/Sapphire (4MB), FireRed/LeafGreen (5MB), Emerald (6MB), Minish Cap (3.1MB), **Mario Kart Super Circuit** (新增)
- **架构**: 输出独立的 C 工程，含游戏代码 + BIOS（也是静态重编译的，不是 HLE stub）+ GBA 硬件 faithful runtime（SDL 后端，带 fallback 解释器 + 运行时 sljit JIT 自愈）
- **优势**: 输出 C 代码可编译到任意平台；不需要自己写 ARM→Thumb-2 翻译器；已经处理了最难的 ARM/Thumb interworking

#### 2.2.1 为什么不直接 ARM7TDMI → Cortex-M33 二进制翻译

> 这是一个自然的问题：GBA 是 ARM7，目标是 Cortex-M33，都是 ARM 架构，能不能跳过 C 直接翻译？

两层翻译链：
```
GBA ROM ──▶ gbarecomp ──▶ C 代码 ──▶ arm-none-eabi-gcc ──▶ Cortex-M33 Thumb-2
                    Layer 1 (gbarcomp)                    Layer 2 (gcc)
```

**Layer 1（ARM7TDMI → C）损失分析**：几乎为 0。ARMv4T 指令语义可以被 C 完全表达（寄存器 → 全局变量；条件执行 → if/switch；load/store → 指针解引用）。gbarecomp 已验证正确。

**Layer 2（C → Cortex-M33 Thumb-2）损失分析**：arm-none-eabi-gcc 是工业级编译器，对 Cortex-M33 的 Thumb-2 指令选择非常成熟。可能比"直接写 Thumb-2 汇编"有 ~5-10% 差距，但：
- 真正的性能瓶颈不是 CPU 指令，而是 **PPU 软渲染（每个像素的 BG/Sprite 合成）**
- PPU/APU/DMA 这些和指令翻译层无关

**如果做直接 ARM7TDMI → Cortex-M33 二进制翻译**：

| ARM7TDMI (ARMv4T) | Cortex-M33 (ARMv8-M Mainline) | 翻译处理 |
|-------------------|-------------------------------|---------|
| ARM 状态 32-bit 指令 | ❌ 不支持 ARM 状态 | 必须翻译成 Thumb-2 32-bit 指令 |
| Thumb-1 16-bit 指令 | ✅ Thumb-2 包含 Thumb-1 子集 | 编码不同但语义相同，大部分可复用 |
| ARM/Thumb interworking | 不需要（只有 Thumb） | 翻译器内部要处理状态切换 |
| SWP/SWPB 原子交换 | ❌ 没有 | LDREX/STREX 或关中断替代 |
| MCR/MRC CP15 协处理器 | ❌ 没有 | NOP 掉或 stub |
| 所有指令条件执行 | ❌ Cortex-M33 只有最后 4 条能条件执行 | 用 IT 指令 + 条件分支组合 |

**结论**：

| 维度 | ARM7 → C → Cortex-M33 (用 gbarecomp) | ARM7 → Thumb-2 直接翻译 |
|------|--------------------------------------|------------------------|
| **开发量** | 用现成工具，0 | 4-6 周（写翻译后端） |
| **输出质量** | gcc O3 → 好 | 直接 Thumb-2 → 可能略好（<10%） |
| **可调试性** | C 代码可读，printf 就能调试 | 机器码不可读，只能 gdb 反汇编 |
| **Runtime** | gbarecomp 已实现完整 C runtime | 需要自己实现（同一份） |
| **self-healing** | gbarecomp 有 fallback 解释器 + 运行时 JIT | 需要自己实现或放弃 |
| **正确性** | 6 个游戏已验证 | 需要重新验证所有游戏 |

**通过 C 翻译的性能损失可以忽略不计**，直接做 ARM7 → Thumb-2 二进制翻译投入产出比太低。

### 2.3 后端替换方案

> **重要**：SDK 和 Solution 是思澈两套不同的软件栈。本项目只用 **SDK v2.5.1**（含 HAL + RT-Thread + BSP），不引入 Solution。所有 API 以 SDK 源码和 `drivers/Include/bf0_hal_*.h` 头文件为准。

| gbarecomp 原始后端 (PC/SDL) | SDK 替换实现 | 依据 |
|---------------------------|--------------|------|
| SDL_Renderer → 渲染窗口 | HAL LCDC (`HAL_LCDC_LayerSetData` + `HAL_LCDC_SendLayerData`) 或 RT-Thread 驱动 `drv_lcd_fb` | `bf0_hal_lcdc.h`, `example/lcd_fb` |
| SDL_AudioDevice → 音频输出 | HAL AUDCODEC (`HAL_AUDCODEC_Transmit_DMA`) | `bf0_hal_audcodec.h`, `example/multimedia/audio/dual_adc_dac` |
| SDL_Event → 按键事件 | HAL GPIO (`HAL_GPIO_ReadPin`) | `bf0_hal_gpio.h`, board.conf KEY1/KEY2 已定义 |
| std::chrono → 时间 | Cortex-M33 DWT cycle counter (内置，直接访问寄存器) | ARM Cortex-M33 架构，SDK `bf0_hal_cortex.h` 有封装 |
| 文件系统 → ROM/保存 | HAL FLASH (`bf0_hal_mpi`) + SPI TF 卡 | SDK `bf0_hal_mpi.h`, `example/storage` |

### 2.4 屏幕方案：居中留黑边

```
390 × 450 屏幕
┌───────────────────────┐
│     黑边 (75px)         │ ← (390-240)/2 = 75
│  ┌─────────────────┐   │
│  │                 │   │
│  │   240×160 GBA   │   │ ← (450-160)/2 = 145 顶部黑边
│  │   帧缓冲         │   │
│  │                 │   │
│  └─────────────────┘   │
│     黑边              │
└───────────────────────┘
```

**实现**：用 LCDC 的 LayerSetData 设置 ROI 区域 (x0=75, y0=145, x1=314, y1=304)，背景层设为黑色。

---

## 3. 实施阶段

### Phase 0：基础验证 — 环境 + LCD 点亮（1-2 天）

**目标**：用 SDK 现成 board 配置，编译并在黄山派上点亮屏幕

**步骤**：
1. `.\set_env.bat gcc` （确认 SDK 已有此文件）
2. 找一个现成 example 适配 board `sf32lb52-lchspi-ulp`
   - 参考路径: `example/lcd_fb/project/` 或 `example/hal_example/project/`
   - scons 命令示例: `scons --board=sf32lb52-lchspi-ulp -j8`
3. 烧录: SifliTrace 或 JLink `sf32lb52-lchspi-ulp` → UART 烧录
4. 验证点: 屏幕点亮，显示色彩变化（参考 lcd_fb example 的彩条逻辑）
5. 用 CoreMark 或简单 DWT 循环测实际性能

**关键参考文件**:
- `e:\work\gitee\sifli\SiFli-SDK\v2.5.1\example\lcd_fb\src\main.c` — 帧缓冲操作完整示例
- `e:\work\gitee\sifli\SiFli-SDK\v2.5.1\customer\boards\sf32lb52-lchspi-ulp\hcpu\board.conf` — 已预配置 LCD/音频/GPIO

**执行结果（2026-11-10，已完成）**：

| 项 | 结果 |
|------|------|
| 编译环境 | `D:\sifli\env`（ENV_VER=1.2.0），arm-none-eabi-gcc 14.2.1，SCons 4.9.1 |
| 验证示例 | `example/lcd_fb`（帧缓冲全屏色块轮换，跑 40s 后退出） |
| 构建命令 | `example/lcd_fb/project` 下 `scons --board=sf32lb52-lchspi-ulp -j8` |
| 产物 | `build_sf32lb52-lchspi-ulp_hcpu/main.bin` 326KB（ROM 354KB/8MB，RAM 232KB/512KB） |
| 烧录命令 | `tools/uart_download/ImgDownUart.exe --port COM14 --baund 1000000 --device SF32LB52X_NOR --file ImgBurnList.ini` |
| 验证结果 | 串口 1Mbps 日志：`CO5300_ReadID 0x331100` → `Found lcd co5300` → `drv_lcd_fb_init done` → `display on`，屏幕点亮 |

**踩坑（务必记住）**：命令行里的路径盘符必须大写 `E:`。用小写 `e:` 会让 `customer/boards/SConscript` 的路径包含判断失效，board 源文件被重复加入，bootloader 链接报 `multiple definition of bsp_init`。

**不走 `set_env.bat`（ConEmu 交互式）时的等价环境变量**：`ENV_ROOT=D:\sifli\env`、`ENV_VER=1.2.0`、`RTT_CC=gcc`、`RTT_EXEC_PATH=D:\sifli\env\tools\gnu_gcc\arm_gcc_14.2.1\bin`、`PYTHONHOME=D:\sifli\env\tools\python-3.11.9-amd64`、`PYTHONPATH=<PYTHONHOME>;<SDK>\tools\build;<SDK>\tools\build\default`、`SIFLI_SDK=<SDK>/`、`SIFLI_SDK_PATH=<SDK>/`、`PATH` 追加 gcc/Python/Scripts/menuconfig/dist/tools\scripts、`LEGACY_ENV=1`。

**未完成**：第 5 步性能基线放到 Phase 2，用真实 PPU 渲染路径测比 CoreMark 更有决策价值。

### Phase 1：PC 端 — gbarecomp 重编译产出 C++ 代码（1 天）

> **为什么不跳过**：`gba_recompile` 必须在 PC 上跑（目标板 Cortex-M33 没有对应的生成器工具链）。这一步只是"在 PC 上跑工具生成代码"，**不是在 PC 上运行模拟器**。
>
> **砍掉了什么**：原版 Phase 1 的"在 PC 上编译运行 + 替换 SDL 后端为 headless stub" — 这部分跳过。SDL 后端直接替换为思澈 SDK HAL 后端（Phase 2 做）。

**目标**：把 Pokemon FireRed ROM 重编译成 **C++20** 代码产物

**构建方式修正（2026-11-10 实测，之前认知有误）**：
- gbarecomp **不是** Rust CLI，**不用 cargo**。`gba_recompile` 是 CMake 项目里的一个 target，工具链是 **MSVC（VS2022/2026）+ Ninja**
- 生成物是 **C++20**（`*.cpp/.h`），runtime 在 `src/gba/*.cpp`、`src/armv4t/*.cpp`
- 三个子模块必须 checkout 到 **pin 的 commit**（不是 master）：
  - `arm-recomp-core` = `63dd32dd210a8bf18bbd11933393a7b87f9b3bf6`
  - `rbengine` = `2a03e73693acee0fb78076ea058642c931bef12e`
  - `recomp-net` = `8c76fb643339144a3a84d5ba8f5defe6250dcd26`

**构建命令（已验证可产出 `build/gba_recompile.exe`）**：
```powershell
# VS DevShell（cmake 3.30.2 没有 VS2026 生成器，必须用 Ninja）
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64'
cmake -S <repo> -B <repo>\build -G Ninja `
  -DGBARECOMP_TOMLPP_INCLUDE_DIR=<...>\third_party\tomlpp-src `
  "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /utf-8" `
  "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /utf-8"
cmake --build <repo>\build --target gba_recompile --parallel 16
```

**两个必踩的坑（务必记住）**：
1. **必须加 `/utf-8`**：源码是 UTF-8 无 BOM，本机 MSVC 默认按代码页 936 解析，`condition_codes.h` 里的 `≥/≤` 注释会把后面的 `GE/LT/AL` 枚举名吃掉，报一堆莫名的 `error C2838: 'GE': illegal qualified name in member declaration`
2. **子模块必须用 pin 的 commit**：用 master 会缺 `ArmCodegen::supports_native_region` / `CodegenCtx::native_region`，编译报 C2039
   （github.com 直连不可用时，用 `api.github.com` 查 pin 的 SHA，再用 `codeload.github.com/<owner>/<repo>/tar.gz/<sha>` 下 tar 包）

**步骤**：
1. ~~安装 Rust~~ 已装（rustup 1.99.1），但本路线实际不需要
2. 源码 + 子模块就位（`third_party/gbarecomp-main`），CMake 构建已通过 → `build/gba_recompile.exe`
3. 准备 ROM/BIOS → 见下方执行结果（实际用 Mario Kart Super Circuit 4MB + 标准 BIOS）
4. 执行重编译 → BIOS 与 ROM 均成功
5. 分析产物 → C++20 / 无内联汇编 / 无 STL，但**代码体积是 ROM 的约 4 倍**
6. 把产物拷贝到 `e:\work\Github\gba_sf32\src\gbarecomp\`（Phase 2 做）

**执行结果（2026-11-10，已完成）**：

输入素材（`e:\work\Github\gba_sf32\roms\`）：

| 文件 | 大小 | SHA-1 | 说明 |
|------|------|-------|------|
| `gba_bios.bin` | 16,384 B | `300C20DF6731A33952DED8C436F7F186D25D3492` | 标准 GBA BIOS |
| `Mario Kart Super Circuit (U).gba` | 4,194,304 B | `9D327C030C3E2D9007990518594F70C3340AC56F` | 4MB，ROM 头校验通过（Title `MARIO KART`、GameCode `AMKE`、HeaderCRC 0x08 匹配） |

> 注：Pokemon FireRed 解压后 16MB，仅 ROM 镜像就超预算，改用它验证；4MB 的 Mario Kart Super Circuit 更贴近硬件上限。

**BIOS 重编译**（`out/bios/`，用仓库自带 `bios/gba_bios.toml`）：

```
==> BIOS mode: rom_base=0x00000000 size=16384 seeds=3
==> discovered 735 functions (arm=97 thumb=638 indirect=361 undefined=0 branch_targets=3133)
```
产物：`bios_recompiled.cpp` 3.39MB / `bios_recompiled.h` 37.7KB / `bios_dispatch_table.cpp` 202KB / `bios_symbol_map.cpp` 28.4KB

**ROM 重编译**（`out/cart/`）：

```
==> discovered 19804 functions (arm=28 thumb=19776 indirect=6172 undefined=52 branch_targets=193880)
==> codegen shards: 8 (adaptive)
elapsed: 9.4s
```

| 项 | 值 |
|------|------|
| 函数数 | 19,804（ARM 28 / Thumb 19,776） |
| 分片 | 8 个 shard + `recompiled.h` + `dispatch_table.cpp` + `symbol_map.cpp` |
| 源码总行数 | 4,269,662 行（平均 216 行/函数） |
| 源码总大小 | ~157MB（单 shard 17.9-23.2MB） |
| 生成耗时 | 9.4 秒 |

**产物兼容性结论**（回答原「分析输出」清单）：

| 问题 | 答案 |
|------|------|
| 产物目录结构 | 纯游戏代码，无 runtime：`recompiled_00N.cpp`（函数体）+ `dispatch_table.cpp`（地址→函数指针表）+ `symbol_map.cpp` + `recompiled.h`。runtime 在仓库 `src/gba/`、`src/armv4t/`，需另行移植 |
| C 还是 C++ | **C++20**（`.cpp`），但语法上是"C 风格的 C++"：只用结构体+全局变量，无类模板 |
| Cortex-M33 不兼容代码 | **无内联汇编、无 SWP/CP15、无编译器特定 attribute**。全目录 `grep __asm\|asm volatile\|std::` 仅命中 2 处 `#include <cstdint>` |
| C++ STL 依赖 | **无**（唯一标准库依赖是 `<cstdint>`） |
| 代码总量 | 见下方"代码膨胀"——**这是当前最大阻塞点** |

**ARM 编译验证（关键实测）**：

用 `arm-none-eabi-g++ 14.2.1 -std=c++20 -Os -mcpu=cortex-m33 -mthumb -fno-exceptions -fno-rtti -fno-threadsafe-statics` 编译 `recompiled_000.cpp`（482k 行）：

| 项 | 值 |
|------|------|
| 编译耗时 | 49.9 秒（单文件！） |
| 错误 | **0** |
| `shard0.o` 文件大小 | 3.23 MB |
| `.text` 段 | 1,987,696 B |
| shard0 函数数 | 2,484 |
| **每函数平均代码量** | **800 B** |

**代码膨胀实测（阻塞点，8 个 shard 全部编译，非推算）**：

全部 8 个 shard 用 `-Os -mcpu=cortex-m33 -mthumb` 编译（8 个并行任务，总耗时 110.9s），`.text` 逐个统计：

| shard | `.text` (B) |
|------|------|
| recompiled_000 | 1,987,696 |
| recompiled_001 | 2,611,604 |
| recompiled_002 | 1,953,384 |
| recompiled_003 | 2,042,640 |
| recompiled_004 | 2,464,900 |
| recompiled_005 | 2,021,540 |
| recompiled_006 | 2,015,396 |
| recompiled_007 | 1,885,016 |
| **合计** | **16,982,176（16.2MB）** |

再加 BIOS 重编译代码（735 函数，预计 ~0.6MB）→ **约 17.6MB ARM `.text`**。

硬件预算：

| 项 | 大小 |
|------|------|
| QSPI NOR 总容量 | 16MB（需扣除 bootloader ~200KB） |
| 重编译代码需求 | **~17.6MB** |
| ROM 镜像（运行期必需） | 4MB（可放 PSRAM） |
| **差额** | **NOR 超出约 2MB** |

结论：**原始生成代码装不下**，需瘦身。

补充实测：`-Oz` 与 `-Os` 产出的 `.text` **完全相同**（1,987,696 B，同 shard），说明膨胀不是优化等级问题，而是生成代码的结构性问题。

**代码瘦身结果（2026-11-10，已实施并验证）**：

改动点：`external/arm-recomp-core/profiles/armv4t_gba/arm_codegen.cpp` 增加环境变量开关 `GBARECOMP_SLIM`，打开后**去掉每指令的 `runtime_should_yield()` 和 `g_runtime_insn_trace` 两个检查**（控制流跳转仍会 `return` 回 dispatch 循环，runtime 仍有机会让出 CPU）。

| 指标 | 原始 | slim | 降幅 |
|------|------|------|------|
| shard0 `.text` | 1,987,696 | 1,480,892 | -25.5% |
| 8 shard 合计 `.text` | 16,982,176 | **12,675,432** | -25.4% |
| shard0 源码行数 | 481,993 | 432,762 | -10.2% |
| 单 shard 编译耗时 | 49.9s | 37.9s | -24% |

最终固件体积预算：

| 项 | 大小 |
|------|------|
| 游戏代码（slim，8 shard） | 12.09MB |
| BIOS 重编译（实测 `.text`） | 0.35MB（slim 后约 0.27MB） |
| runtime（CPU/PPU/APU/DMA，估算） | 0.5-1MB |
| **合计** | **约 13.0-13.6MB** |
| NOR 扣 bootloader 后可用 | 约 15.7MB |
| **余量** | **约 2MB** ✅ 放得下 |

ROM 镜像 4MB 放 PSRAM（8MB），不占 NOR。

重新编译尺寸的完整命令（8 并行，69.4s）：

```powershell
$env:GBARECOMP_SLIM="1"
gba_recompile.exe --rom "roms\Mario Kart Super Circuit (U).gba" --out out\cart_slim
# 然后每个 shard 用 arm-none-eabi-g++ -std=c++20 -Os -mcpu=cortex-m33 -mthumb \
#   -fno-exceptions -fno-rtti -fno-threadsafe-statics -c 编译
```

⚠️ 语义影响：每指令 yield 检查被移除后，抢占粒度变粗（只在实际分支/函数返回时让出）。周期计数 `runtime_tick` 仍然保留在每条指令，所以**时序精度不变**，只是 IRQ/音频的响应延迟从"每指令"变成"每控制流跳转"。对绝大多数游戏无影响，但需要在 hardware 上实测确认。

**膨胀根因**（已定位到 generator 源码 `external/arm-recomp-core/profiles/armv4t_gba/arm_codegen.cpp`）：

1. 每条 guest 指令一个 `{ }` 块，块首无条件插入 `if (runtime_should_yield()) return;`（第 1555/1700 行）
2. 每条指令维护 `_cyc_XXXXXXXX` 周期累加变量，读 `g_runtime_waits.s32[8]`（第 184/195 行）
3. 每条指令写 `g_cpu.R[15] = <next_pc>`
4. 这三项都是**硬编码，无编译开关**

→ 瘦身方向：改 `arm_codegen.cpp` 去掉每指令 yield 检查与周期计数（保留函数级检查），预计可降到 1/3；副作用是 GBA 周期精确性下降（对大多数游戏无感），同时帧率大幅提升。

### Phase 2：思澈平台移植 — CPU + LCD（进行中）

**目标**：在黄山派上跑通重编译的 CPU + 画面

**执行状态（2026-11-10）**：

| 步骤 | 内容 | 状态 |
|------|------|------|
| 2-A | 仓库根独立 scons 工程骨架，能编出可烧录固件 | ✅ 编译通过 |
| 2-B | 显示后端：LCD 中央 240×160 写 RGB565 彩条 | ✅ 已烧录验证（当时看到的「滚动彩条」实为 165×15 的坐标 bug，见 2-C 修复） |
| 2-C | runtime 移植（`src/gba/*` + `runtime_arm.cpp` + `runtime_bus_bridge.cpp` 编入工程） | ✅ 真机跑通，居中 240×160 已复烧确认 |
| 2-D | 接上卡带：小测试 ROM 验证 cart → dispatch → PPU → 送屏链路 | ⏳ 方案已定，未编码 |
| 2-E | 真实 ROM 接入：容量优化（dispatch 压缩 / -Oz / ROM LZ4）+ 端到端验证 | ⏳ 未开始 |
| 2-F | 按键 / 音频 / 存档 | ⏳ 未开始（Phase 3） |

**工程结构（已落地，独立于 SDK 目录）**：

```
e:\work\Github\gba_sf32\
├── SConstruct              # 抄 SDK example/lcd_fb/project 的骨架
├── SConscript              # 先加载 <SDK>/SConscript，再加载 src/SConscript
├── Kconfig                 # source "$SIFLI_SDK/Kconfig.v2" + rsource "Kconfig.proj"
├── Kconfig.proj            # CUSTOM_MEM_MAP + BSP_USING_LCD_FRAMEBUFFER
├── proj.conf               # 主线程栈/优先级、ULOG 配置
├── rtconfig.py             # 空文件（必须存在，否则 rtconfig.LINK_SCRIPT_SRC 为 None）
├── src/
│   ├── SConscript          # gba_app（main.c + 宿主层 + BIOS blob）+ gba_runtime（gbarecomp）
│   ├── main.c              # LCD 后端 + GBA arena，调 gba_device_run()
│   ├── gba_device.cpp      # 宿主层：placement new Bus/Ppu、BIOS 装载、dispatch 主循环
│   └── gba_device_stubs.cpp# overlay/dispatch-miss/未实现指令等 abort 桩
├── plan/gba_hsp_plan.md
├── README.md
└── third_party/gbarecomp-main/   # 生成器源码（.gitignore 忽略，不入库）
```

构建命令（在仓库根执行，路径盘符建议大写）：

```powershell
scons --board=sf32lb52-lchspi-ulp -j8
```

产物：`build_sf32lb52-lchspi-ulp_hcpu/`（`main.bin`、`bootloader/`、`ftab/`、`ImgBurnList.ini`、`sftool_param.json`、`uart_download.bat`）

**2-A/2-B 实测结果**：

| 项 | 值 |
|------|------|
| 编译结果 | 成功，无 warning 阻断 |
| `main.bin` | 325,240 B |
| ROM 占用 | 354 KB / 8 MB |
| RAM 占用 | 133,980 B / 512 KB（全屏帧存放在 PSRAM，不计入 L1） |
| 烧录镜像 | 3 个：bootloader@0x12010000、main@0x12020000、ftab@0x12000000 |

`main.c`（2-B 版本）：L1 段（片上 SRAM）放 240×160 RGB565 帧存并逐帧生成滚动彩条 + 中心黑方块；L2 段（OPI-PSRAM）放 390×450 全屏帧存并用 `drv_lcd_fb_write_send()` 只刷新中央 (75,145)-(314,304) 区域；串口每秒打印 `display: N fps`。

**2-C 实测结果（2026-11-10）**：

| 项 | 值 |
|------|------|
| 编译结果 | 成功（仅 2 条 `initialized and declared 'extern'` 无害告警） |
| `main.elf` | 9,279,008 B（含符号）；二进制 914,212 B |
| ROM 占用 | 944,040 / 8,388,608 B |
| RAM 占用 | 199,296 / 523,264 B（`.bss` 77,988 B） |
| PSRAM 占用 | arena 1MB + 冷缓冲 arena 4MB + LCD 帧存 343KB，均在 `0x60xxxxxx` |
| 链接 | 无 `region 'RAM' overflowed`，全部符号解析 |

**2-C 落地内容**：

- `SConstruct`：全局 `CXXFLAGS += -std=c++20 -fexceptions -funwind-tables -fno-threadsafe-statics`（runtime 用 `throw` 退出 dispatch，SDK 默认 `-fno-exceptions`）。
- `src/SConscript`：`gba_app` 组（`main.c`、`gba_device.cpp`、`gba_device_stubs.cpp`、`gba_psram.c`、生成的 `out/gen/gba_bios_blob.c`）+ `gba_runtime` 组（`src/gba/*.cpp`、`runtime_arm.cpp`、`symbol_lookup.cpp`、`runtime_bus_bridge.cpp`、`mod_function_hooks.cpp`、`out/bios/bios_recompiled.cpp`、`bios_dispatch_table.cpp`）。BIOS 二进制在 scons 阶段转成 C 数组，免去运行时文件系统。
- `gba_psram.c`（新增）：`L2_NON_RET_BSS` 静态预留 4MB PSRAM arena，提供 bump 分配器 `gba_psram_alloc/free`（分配即清零，运行期不回收）。参考 InfoNES / PureDOOM 的 PSRAM 用法，把大块低频缓冲移出片上堆。
- `gba_device.cpp`：PSRAM arena 上 placement new `GbaBus`/`GbaPpu`，装载 `roms/gba_bios.bin`（校验 SHA1），`set_active_bus/ppu`、`runtime_init`，复位 CPU 后 `for(;;) runtime_dispatch(g_cpu.R[15])`；BIOS 阶段用 `g_runtime_fn_entry_hook` 打「首次进入某 BIOS 函数」日志（SVC/IRQ 模式下 `runtime_should_yield` 不触发 present），PC 越过 `0x08000000` 视为 BIOS 引导结束。启动时额外打印片上堆与 PSRAM arena 的用量（`[gba] heap ...` / `[gba] psram ...`）。
- `gba_device_stubs.cpp`：`overlay_try_dispatch` 恒 0、`runtime_dispatch_miss`/`runtime_unimplemented_op`/`runtime_force_interp_step`/`runtime_bridge_interpret` 打印后死循环、空 `kDispatchTable`、`armv4t::mul_wait_cycles`（照抄 `arm_ir.cpp`）、`gbarecomp::multiplayer_slice_safe` 恒 false。

**third_party 补丁（不侵入 gbarecomp 逻辑，全部用宏门控）**：

| 文件 | 宏 | 作用 |
|------|-----|------|
| `src/gba/gba_ppu.h` | `GBARECOMP_COMPACT_FRAMEBUFFER` | 帧存改 `std::array` 240×160×3，替代 1280×864×3 堆缓冲 |
| `src/runtime/runtime_bus_bridge.cpp` | `GBARECOMP_NO_HOST_SAMPLER` | 去掉 `<thread>` 采样线程 |
| `src/armv4t/runtime_arm.cpp` | `GBARECOMP_SMALL_TRACE_RING` | trace ring 4096 → 64 条（省 288 KB `.bss`） |
| `src/runtime/runtime_bus_bridge.cpp` | `GBARECOMP_NO_HANG_TRACE_DUMP` | 去掉 hang dump 的 4096×72 B 静态 ring（再省 288 KB） |
| `src/gba/gba_audio.h` + 新增 `gba/psram_alloc.h` | `GBARECOMP_PSRAM_ALLOC` | 采集环恢复 262144 条（3.5MB），改用 `PsramAllocator` 从 PSRAM arena 分配，不占片上堆 |
| `src/gba/gba_color.h` | — | `std::min<uint32_t>(...)` 显式特化（ARM 上 `uint32_t` 是 `unsigned long`，模板推导失败） |

上表的 6 个宏（含 `GBARECOMP_SMALL_AUDIO_CAPTURE` 降级档）已改成 **Kconfig 选项**：符号定义在 `Kconfig.proj` 的 `menu "GBARECOMP runtime"`，默认值写在 `proj.conf`，用 `scons --menuconfig` 开关。因为 gbarecomp 源码不 include `rtconfig.h`，由 `src/SConscript` 用 `GetDepend()` 读出后映射进 `gba_app` / `gba_runtime` 两个 group 的 `CPPDEFINES`，构建时打印 `[gba] runtime features: ...` 便于确认实际生效的开关。

**主线程栈**：`render_scanline_impl` 的栈帧是 12,428 B（`sub.w sp, sp, #12416` + `sub sp, #12`），默认 8 KB 主线程栈不够，写 SP 越过栈下限触发 `SCB_CFSR_UFSR:0x10 STKOF` 硬故障（崩在 `[gba] bios enter` 之后的首次 scanline 渲染）。`proj.conf` 的 `CONFIG_RT_MAIN_THREAD_STACK_SIZE` 已改为 32768；`RT_USING_HEAP` 下主线程栈是 `rt_thread_create()` 从堆分配，不进 `.bss`，因此 RAM 静态占用不变（199,296 B），运行期堆多占 24 KB。

**Phase 2-C 真机结果（2026-11-11，`窗口1_COM14_(2026-11-11-09-04-13)_ui.log`）**：无 fault；`heap ... used=230644`（含 32 KB 主线程栈）、`psram used=3670016 of 4194304`；BIOS 从 `00000000` 跑到 `00000348` 一带的 SWI/IRQ 空闲循环，共 236 个**首次**函数入口（`bios_entry_hook` 每 4 字节 BIOS 字只记录一次，不是 256 条上限截断）。**但屏幕只出现一条约 165×15 的白条（实拍照片核对），不是 240×160**：`g_src_area` 误用了 GBA 自身坐标系 `(0,0)-(239,159)`，而 `g_lcd_area` 用的是面板坐标 `(75,145)-(314,304)`，驱动 `area_intersect()` 求交后只剩 `(75,145)-(239,159)` = 165×15；Phase 2-B 的「滚动彩条」其实是同一个 bug（同一位置同一尺寸），不是设计效果。白色本身来自 `render_scanline_impl` 的 DISPCNT bit7 forced-blank 分支 `memset(row, 0xFF, ...)`（RGB565 0xFFFF），说明 present 回调与 PPU 扫描线渲染链路是通的，问题只在送屏几何。

**修复（2026-11-11）**：`drv_lcd_fb` 的 `write_fb_async()` 里 `dst_line_addr` 不含 x 偏移，且驱动始终按 `fb.area` 的像素量刷新 `fb_clip`（LCDC 用 `data_area` 左上角作图层摆放位置），所以**不支持**在一个全屏帧存里写局部 x 区间。改为把 GBA 窗口本身注册成一个 240×160 的帧存（`area=(75,145)-(314,304)`、`line_bytes=480`、`p_data=g_gba_fb`）：每帧把 RGB565 直接转进 `g_gba_fb`，再用同一区域同时作 `write_area`/`src_area` 调 `drv_lcd_fb_write_send()`，走 `src_line_addr == dst_line_addr` 的 skip 分支，只 flush 76.8 KB 而不是 351 KB 全屏。全屏帧存 `g_lcd_fb` 只保留给开机一次性清边（之后 `drv_lcd_fb_set()` 切到窗口帧存）。另外 `gba_lcd_present()` 里不再调 `drv_lcd_fb_wait_write_done()`：它会清掉 `EVENT_WRITE_DONE`，导致下一次 `write_send()` 在 1167 行干等 100 ms 超时（这解释了日志里 2.8 s 没有一条 `[gba] frame` 打印）；`send=1` 本身已经在等上一帧 flush 完成。

**复烧验证（2026-11-11）**：白区变成**居中的 240×160**，位置 `(75,145)-(314,304)`，尺寸与预期一致；说明窗口帧存 + 面板偏移的用法在 CO5300 上成立。Phase 2-C 验收通过。下一步是 Phase 2-D：把重编译后的 ROM 挂到总线（`set_rom`）并链接 shard 产物。相关的 SDK 文件（`drv_lcd_fb.c` / `drv_lcd.c` / `bf0_hal_lcdc.c` / `co5300.c`）全程只读，未做任何修改。

**构建环境（不走 `set_env.bat` 交互式 ConEmu 时）**：

| 变量 | 值 |
|------|-----|
| `SIFLI_SDK` | `<SDK>/` |
| `ENV_ROOT` / `ENV_VER` | `D:\sifli\env` / `1.2.0` |
| `RTT_CC` | `gcc`（不设会让 `GetLinkScript` 返回 None） |
| `RTT_EXEC_PATH` | `D:\sifli\env\tools\gnu_gcc\arm_gcc_14.2.1\bin` |
| `PYTHONHOME` | `D:\sifli\env\tools\python-3.11.9-amd64` |
| `PYTHONPATH` | `<PYTHONHOME>;<SDK>\tools\build;<SDK>\tools\build\default` |
| SCons | 必须用 `%PYTHONHOME%\Scripts\scons.exe`（PATH 里若有 Python2.7 版 scons 会报错） |

**Phase 2 新踩的坑**：

1. `rtconfig.py` 必须是空文件但**必须存在**，否则 `rtconfig.LINK_SCRIPT_SRC` 为 `None`，链接阶段抛 `AttributeError`。
2. `drv_lcd_fb_send()` 在本 SDK 只有声明没有实现，链接期 `undefined reference`；初始清屏改用 `drv_lcd_fb_write_send()`。
3. `drv_lcd_fb_write_send()` 的 `write_area` 与 `src_area` 必须处在**同一坐标系**（都按 `fb.area` 的绝对坐标），驱动只对二者求交；且它按 `fb.area` 的像素量整块刷新 `fb_clip`，`dst_line_addr` 不含 x 偏移，因此**不支持**把局部 x 区间写进一张全屏帧存 —— 要显示小于面板的窗口，就把窗口本身注册成帧存。
4. 工程路径盘符必须大写 `E:`（小写会让 board 源文件被重复加入，报 `multiple definition of bsp_init`）。

**烧录结果（2026-11-10，已解决）**

- 失败经历：`ImgDownUart.exe` 在 `downloadfile bootloader` 处 3 秒超时；`sftool` 早期报 `Error reading frame start marker / Failed to connect to the chip`，根因是板子当时未进入 UART 下载模式，不是工具问题。
- 最终可用命令：在 `build_sf32lb52-lchspi-ulp_hcpu/` 下执行 `uart_download.bat`，输入端口号 `14`，其内部等价命令为：
  ```
  sftool -p COM14 -c SF32LB52 -m nor write_flash "bootloader\bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab\ftab.bin@0x12000000"
  ```
- `sftool` 位置：`C:\Users\zhang\.sifli\tools\sftool\0.2.5\sftool.exe`（需在 PATH 中），参数取自同目录 `sftool_param.json`（`chip=SF32LB52`、`memory=NOR`）。
- 验证：烧录后屏幕在中央 240×160 区域出画面（2-B 时看到的是 165×15 细条，坐标 bug；2-C 修复后为居中 240×160）。

**2-C runtime 移植方案（已调研确认）**：

gbarecomp 的 PC 宿主层不能直接用：`runtime.cpp`（246KB）主循环里用 `std::thread`，`host_window.cpp` 是 SDL 后端。设备端只复制"纯逻辑层"：

| 可整段复用 | 说明 |
|------------|------|
| `src/gba/*.cpp` | CPU/PPU/APU/DMA/IO/卡带 |
| `src/armv4t/runtime_arm.cpp` | 重编译代码的 dispatch 主体（需 stub `overlay_try_dispatch`） |
| `src/runtime/runtime_bus_bridge.cpp` | 总线桥接，自身已实现 `runtime_tick` |
| `tools/test_rom_runner/main.cpp` | 宿主层模板（约 70 行） |

不链接：`runtime.cpp`、`host_window.cpp`、`host_platform.cpp`、`launcher_seam.h`、`diagnostic_replay`、`overlay_loader`、`mod_*`、`multiplayer_*`、`tcp_debug_server`、`asset_picker`、`mobile_platform`。

设备端宿主层要提供：

1. `gbarecomp::set_active_bus()` / `set_active_ppu()` 绑定
2. `int overlay_try_dispatch(...)` → 恒返回 0
3. `runtime_dispatch_miss` / `runtime_unimplemented_*` / `runtime_force_interp_step` → abort（便于暴露未覆盖指令）
4. `runtime_set_frame_present_hook(std::function<bool()>)`：在 `runtime_should_yield` 的 VBlank 分支就地回调，主线程只调用一次 `runtime_dispatch(0x08000000)`，永不返回
5. present 回调里 `ppu.latched_framebuffer()` → RGB888（stride = `render_width()*3`）转 RGB565 写入 LCD
6. `bus.io().set_keyinput(uint16_t)` 喂 GPIO 按键

需要打补丁的地方：`runtime_bus_bridge.cpp:33` 的 `<thread>` 采样线程（`GBARECOMP_SAMPLE` 门控）、`runtime_arm.cpp:58/:1077` 的 overlay_loader 依赖、`runtime_arm_default_aborts.cpp:52/:67` 的 `<filesystem>` / `<unistd.h>`。`reset_recomp_cpu` 是 `runtime.cpp:1236` 的 file-static，不可链接，需照抄（PC/R13/banked_sp/cpsr）。

**关键实现**：

#### 3.2.1 渲染后端

```c
// render_backend.c 核心逻辑
// 参考 example/lcd_fb/src/main.c 的 drv_lcd_fb API

// 屏幕参数
#define LCD_W       390
#define LCD_H       450
#define GBA_W       240
#define GBA_H       160
#define OFFSET_X    ((LCD_W - GBA_W) / 2)   // 75
#define OFFSET_Y    ((LCD_H - GBA_H) / 2)   // 145

// 全局帧缓冲（GBA 分辨率，76.8KB）
static L1_NON_RET_BSS_SECT(frambuf, ALIGN(64) uint8_t gba_fb[GBA_W * GBA_H * 2]);

// LCDC 句柄 + drv_lcd_fb_desc
static lcd_fb_desc_t fb_desc = {
    .area = {0, 0, LCD_W - 1, LCD_H - 1},
    .p_data = ...,   // 390x450 屏幕帧缓冲（SDK 分配）
    .format = RTGRAPHIC_PIXEL_FORMAT_RGB565,
    .line_bytes = LCD_W * 2,
};

void render_init(void) {
    drv_lcd_fb_init("lcd");
    drv_lcd_fb_set(&fb_desc);
    // 清屏为黑色
    epic_fill(LCD_BLACK);
}

void render_blit_gba(uint16_t* gba_pixels) {
    // 居中区域 ROI
    LCD_AreaDef roi = {OFFSET_X, OFFSET_Y, OFFSET_X + GBA_W - 1, OFFSET_Y + GBA_H - 1};
    // 从 GBA 帧缓冲复制到屏幕帧缓冲的居中位置
    drv_lcd_fb_write_send(&roi, &roi, (uint8_t*)gba_pixels, NULL, 1);
}
```

#### 3.2.2 时间后端

```c
// 用 Cortex-M33 DWT cycle counter
// 240MHz / 16.78MHz ≈ 14.3x
// GBA 一帧 ≈ 16777216 / 59.73 ≈ 280960 cycles
// 我们的 GBA 帧循环需要 sleep (280960 * 14.3) ≈ 4M cycles

#define GBA_CYCLES_PER_FRAME  280960ULL
#define CPU_CLOCK_HZ          240000000ULL
#define GBA_CPU_CLOCK_HZ      16777216ULL
#define CYCLES_PER_GBA_FRAME  (GBA_CYCLES_PER_FRAME * CPU_CLOCK_HZ / GBA_CPU_CLOCK_HZ)

static inline uint32_t dwt_get_cycles(void) {
    return DWT->CYCCNT;
}

static inline void dwt_reset(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void gba_frame_wait(uint32_t start_cycles) {
    uint32_t elapsed = dwt_get_cycles() - start_cycles;
    if (elapsed < CYCLES_PER_GBA_FRAME) {
        uint32_t wait = CYCLES_PER_GBA_FRAME - elapsed;
        // 用 rt_thread_delay 或 spin loop 精确等待
        // RT-Thread tick 一般 1ms，不够精确
        // 用 busy wait + SysTick 组合
    }
}
```

#### 3.2.3 Phase 2 验证清单

| # | 验证点 | 通过标准 | 状态 |
|---|--------|---------|------|
| 2.1 | LCD 屏幕点亮 | 全黑背景 390×450 | ✅ 通过 |
| 2.2 | 居中区域可写 | 中央 240×160 显示测试色块 | ✅ 通过（2026-11-11 复烧确认居中 240×160 白块） |
| 2.3 | CPU 跑通 BIOS | 串口打印 BIOS 初始化日志 | ✅ 通过（236 个函数入口后进入空闲循环） |
| 2.4 | PPU 软渲染输出 | GBA 画面正确显示在居中区域 | ✅ 通过（无卡时 forced blank 的 240×160 白块正好居中；待挂 ROM 后看真实画面） |
| 2.5 | 颜色格式正确 | BGR555 → RGB565 颜色无偏差 | ⏳ 未开始 |
| 2.6 | 跑通游戏标题画面 | Mario Kart Super Circuit 标题显示 | ⏳ 未开始 |

#### 3.2.4 Phase 2-E 容量优化候选（暂不实施，待 2-D 真机跑通真实 ROM 后再定）

> 决策依据：2026-11-12 实测 Phase 2-D 链接结果 `main.bin` 16,342,472 B / 分区 16,613,376 B，
> 余量 264 KB——当前这一款 4 MiB ROM 已经能装下，所以 2-E **不作为阻塞项**。
> 下面三项按收益排序记录，等 TF 卡真实游戏验证通过后按需取用。

| # | 措施 | 现状 | 预期收益 | 改动位置 / 风险 |
|---|------|------|---------|----------------|
| E1 | **基本块内寄存器提升**：把 guest R0–R7 提到 C 局部变量，块内只读写局部，块末写回 | 已确认 gbarecomp 完全没做，每条指令直读直写全局 `g_cpu.R[n]` | `.text` 降 **20–35%**（12.8 MB → 8–10 MB） | 改 `arm_codegen.cpp`，加 `GBARECOMP_REG_CACHE` 开关；需跳过含 PC 写入、bank 切换、`bus_*` 回调的基本块。风险中等 |
| E2 | **派发表压缩**：`kDispatchTable` 12 B/条 × 167,910 → 8 B/条 + 区间编码 | 2,014,920 B | 压到 **约 0.9 MB**，省 ~1.1 MB | 纯数据格式，离线生成 + 运行时查表改动；风险低 |
| E3 | **`ram_entries` 去重**：与 `kDispatchTable` 合并 | 27,078 条 × 16 B = 433,760 B | 可省大半（~0.3 MB） | 与 E2 一起做；风险低 |

E1 + E2 合计可把 15.585 MiB 压到 **约 12 MiB**，为 6 MiB 级 ROM 腾出空间。
`-Oz` / 手写 Thumb-2 汇编**不是**候选：前者与 `-Os` 产出完全相同（已验证），后者对 Cortex-M33 的收益 <10%，
且体积瓶颈在访存搬运而非指令选择。

> 已否决的路线（见 2.2.1 与 2026-11-12 讨论）：把 GBA 原始指令字节直接交给 Cortex-M33 执行，
> 只翻译一部分。虽然两者都是 ARM，但 Thumb-1 ⊂ Thumb-2 之外还有 4 个必要条件不满足
> （地址空间 `0x08000000`/`0x02000000`/`0x03000000` 无法重映射、寄存器模型、PC+8 vs PC+4、SWI/IRQ vs SVC/NVIC），
> 其中地址重映射需要 MMU，而 Cortex-M33 的 MPU 只能改权限、不能改地址——硬性否决。

### Phase 3：音频 + 输入 + 保存（3-4 天）

#### 3.3.1 音频后端

```c
// audio_backend.c
// 使用 SDK HAL_AUDCODEC + DMA
// 参考 example/multimedia/audio/dual_adc_dac/src/main.c

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUF_SAMPLES 1024   // DMA buffer 大小

static AUDCODEC_HandleTypeDef haudcodec;
static int16_t audio_dma_buf[AUDIO_BUF_SAMPLES * 2];  // 交错 L/R
static volatile uint32_t audio_dma_idx = 0;

void audio_init(void) {
    // 1. 配置 DAC 时钟 (44100Hz)
    AUDCODE_DAC_CLK_CONFIG_TYPE dac_clk = {
        .samplerate = AUDIO_SAMPLE_RATE,
        .clk_src_sel = 1,  // PLL 44.1M
        .osr_sel = 4,       // 64x oversampling
    };
    // 2. 初始化 AUDCODEC
    HAL_AUDCODEC_Config_Analog_DACPath(&dac_clk);
    // 3. 配 DMA
    // 4. 启动 DMA 发送
    HAL_AUDCODEC_Transmit_DMA(&haudcodec, (uint8_t*)audio_dma_buf,
                              sizeof(audio_dma_buf), dma_channel);
}

// DAC DMA 完成回调 — 从 gbarecomp runtime 拉数据填入 buffer
void HAL_AUDCODEC_TxCpltCallback(AUDCODEC_HandleTypeDef *h) {
    gbarecomp_audio_mix(audio_dma_buf, AUDIO_BUF_SAMPLES);
}
```

#### 3.3.2 PSG 软件合成

GBA 的 4 通道 PSG 非常简单：

```c
// Channel 1/2: 方波，可变频 + duty cycle (12.5/25/50/75%)
// Channel 3: 波形 (32 × 5-bit 用户自定义)
// Channel 4: 噪声 (LFSR)

typedef struct {
    uint32_t freq;          // Hz
    uint8_t duty;           // 0-3
    int16_t volume;         // 当前音量 (envelope)
    uint32_t phase_accum;   // 相位累积
} gba_psg_t;

// 方波合成（GBA PSG 占空比映射）
static int16_t psg_square_step(gba_psg_t* ch) {
    static const uint8_t duty_pct[4] = {2, 4, 8, 12};  // 12.5%, 25%, 50%, 75%
    uint32_t threshold = (1ULL << 32) * duty_pct[ch->duty] / 16;
    ch->phase_accum += (uint64_t)ch->freq * (1ULL << 32) / AUDIO_SAMPLE_RATE;
    return (ch->phase_accum < threshold) ? ch->volume : -ch->volume;
}
```

#### 3.3.3 按键输入

```c
// input_backend.c
// KEY1 = PA34, KEY2 = PA43
// board.conf 中已配置 CONFIG_BSP_USING_KEY1/KEY2=y

#include "drv_gpio.h"

#define KEY1_PIN 34
#define KEY2_PIN 43

// 长按 KEY2 = Start, 短按 = A
// KEY1 = B, 长按 KEY1 = Select
// 还缺 D-pad/L/R — 先用默认值（未按下）

#define GBA_KEY_A      (1 << 0)
#define GBA_KEY_B      (1 << 1)
#define GBA_KEY_SELECT (1 << 2)
#define GBA_KEY_START  (1 << 3)
// D-pad/L/R 未映射，暂时返回 0

static uint32_t key_press_ticks[2] = {0};

uint16_t input_read_keypad(void) {
    uint16_t keypad = 0;
    int key1 = gpio_read(KEY1_PIN);
    int key2 = gpio_read(KEY2_PIN);

    if (!key2) {
        key_press_ticks[1]++;
        if (key_press_ticks[1] < 300)       keypad |= GBA_KEY_A;
        if (key_press_ticks[1] > 300)       keypad |= GBA_KEY_START;
    } else {
        key_press_ticks[1] = 0;
    }

    if (!key1) {
        key_press_ticks[0]++;
        if (key_press_ticks[0] < 300)       keypad |= GBA_KEY_B;
        if (key_press_ticks[0] > 300)       keypad |= GBA_KEY_SELECT;
    } else {
        key_press_ticks[0] = 0;
    }

    return keypad;
}
```

#### 3.3.4 保存功能

```c
// storage_backend.c
// GBA 保存: EEPROM 128B-64KB / SRAM 32-64KB / Flash 64KB-128KB
// Pokemon Emerald 用 1MB Flash，Ruby/Sapphire 用 EEPROM+SRAM
// 映射到 NOR Flash 分区（SDK 自带 FAL 分区表 + FlashDB）

// Phase 3 MVP：用 RAM 缓存 + 关机时/定时刷 Flash
// Phase 4 完善：用 SPI TF 卡做大容量保存
```

#### 3.3.5 Phase 3 验证清单

| # | 验证点 |
|---|--------|
| 3.1 | DAC 输出正弦波 → 听到声音 |
| 3.2 | GBA PCM DMA → 游戏音效正常 |
| 3.3 | PSG 4 通道合成 → 背景音乐可听 |
| 3.4 | KEY1/KEY2 响应 → 能控制游戏 |
| 3.5 | 长按检测 → Start/Select 映射正确 |
| 3.6 | 保存/加载 → 存档成功 |
| 3.7 | 完整游戏流程: 标题 → 对话 → 菜单 → 存档 → 重启 |

### Phase 4：完善与优化（持续）

**性能目标**: CPU 占用 < 80%，音频延迟 < 50ms，帧率稳定 60fps

**优化方向**:
1. **Tile 预解码** — PPU 模拟时，背景 Tile (8×8 索引色) 预生成位图 → 渲染快 2-3x
2. **帧缓冲优化** — 让 LCDC 直接用 GBA 帧缓冲作为 Layer data，省去 blit
3. **Cortex-M33 DSP 扩展** — 音频混合用 SIMD → 快 5-10x
4. **中断优先级** — 音频 DMA > LCDC > Timer > 其他 → 减少抖动
5. **RTOS 调度** — 把渲染/音频/游戏循环分到不同线程
6. **ePicasso GPU 辅助** — 用 Epic 引擎做 Tile 解码加速

**ROM 大小处理**:
- ≤ 8MB → 放 NOR Flash + PSRAM 运行
- > 8MB → 放 SPI TF 卡，运行时按需加载大块数据

**测试 ROM 优先级**（均 < 8MB，gbarecomp 已验证）:
| 优先级 | 游戏 | ROM | 大小 |
|--------|------|-----|------|
| P0 | Pokemon Ruby/Sapphire | 4MB | 重编译已验证 |
| P1 | Pokemon FireRed/LeafGreen | 5MB | 重编译已验证 |
| P1 | Pokemon Emerald | 6MB | 重编译已验证 |
| P2 | The Minish Cap | 3.1MB | 重编译已验证 |
| P3 | 更多 < 8MB 游戏 | — | 逐一测试 |

---

## 4. 关键风险

| 风险 | 概率 | 影响 | 处理 |
|------|------|------|------|
| **【已解决】重编译代码体积 ≈ ROM 的 4 倍** | 已发生 | 高 | 原始 16.98MB 装不下；用 `GBARECOMP_SLIM` 去掉每指令 yield/trace 检查后降到 **12.68MB**，加 BIOS/runtime 约 13.6MB，NOR 余量约 2MB |
| gbarecomp C 代码在 Cortex-M33 效率不够 | 中 | 高 | 先测实际 CPU 占用；必要时改用 rt-thread 线程调度 / 降低帧率 |
| PPU 软渲染帧率不够 60fps | 中 | 高 | Tile 预解码 → DSP SIMD → ePicasso GPU |
| 游戏 ROM > 8MB 放 NOR 装不下 | 低 | 中 | 自动切 TF 卡方案 |
| gbarecomp 对目标 ROM 覆盖率不足（fallback 太多） | 中 | 中 | 先用 P0/P1 游戏；manifest 反馈补全 |
| 颜色格式 BGR555 vs RGB565 色偏 | 低 | 低 | blit 时加格式转换 |

---

## 5. 参考文件速查

### 本地 SDK
| 用途 | 路径 |
|------|------|
| SDK 根 | `e:\work\gitee\sifli\SiFli-SDK\v2.5.1\` |
| 环境脚本 | `set_env.bat` |
| 构建命令 | `scons --board=sf32lb52-lchspi-ulp -j8` |
| Board 配置 | `customer/boards/sf32lb52-lchspi-ulp/hcpu/board.conf` |
| RT-Thread | `rtos/` |
| HAL 驱动头 | `drivers/Include/bf0_hal_*.h` |
| HAL 驱动实现 | `drivers/hal/bf0_hal_*.c` |
| 关键 HAL | `bf0_hal_lcdc.h`, `bf0_hal_audcodec.h`, `bf0_hal_gpio.h`, `bf0_hal_dma.h` |
| LCDC API | `HAL_LCDC_Init`, `HAL_LCDC_LayerSetData`, `HAL_LCDC_SetROI`, `HAL_LCDC_SendLayerData` |
| 音频 API | `HAL_AUDCODEC_Init`, `HAL_AUDCODEC_Transmit_DMA`, `HAL_AUDCODEC_Config_TChanel` |
| RT 设备驱动 | `middleware/rt-thread/components/drivers/` |
| `drv_lcd_fb` | LCD 帧缓冲抽象层，参考 example/lcd_fb |
| `drv_epic` | ePicasso 2.5D GPU，参考 example/lcd_fb |

### 参考 Example
| Example | 路径 | 学什么 |
|---------|------|--------|
| lcd_fb | `example/lcd_fb/` | 帧缓冲初始化 + 双缓冲 + fill |
| lcd_stress_test | `example/lcd_stress_test/` | LCD 基础操作 |
| hal_example | `example/hal_example/` | HAL 层各外设 |
| dual_adc_dac | `example/multimedia/audio/dual_adc_dac/` | AUDCODEC DAC 配置 |
| local_music | `example/multimedia/audio/local_music/` | 音频 DMA 播放 |

### 重编译
| 项目 | 地址 |
|------|------|
| gbarecomp 引擎 | https://github.com/mstan/gbarecomp |
| FireRed 重编译工程 | https://github.com/mstan/FireRedLeafGreenRecomp |
| Emerald 重编译工程 | https://github.com/mstan/EmeraldRecomp |
| Minish Cap 重编译工程 | https://github.com/mstan/MinishCapRecomp |

### 在线文档

> **注意**：本项目只用 SDK，不用 Solution。Solution 文档（docs.sifli.com/projects/solution/）描述的是另一套软件栈，**API 不保证适用于 SDK**。以下仅列 SDK 相关的文档和社区资源。所有 HAL API 和 RT-Thread 驱动用法，**以 `drivers/Include/bf0_hal_*.h` 头文件注释 + `drivers/hal/bf0_hal_*.c` 实现 + `example/` 下示例为准**。

| 主题 | URL | 说明 |
|------|-----|------|
| SDK SF32LB52X 文档 | https://docs.sifli.com/projects/sdk/latest/sf32lb52x/ | SDK 级文档（芯片架构、HAL 速查） |
| OpenSiFli GitHub | https://github.com/OpenSiFli/ | 思澈 SDK + BSP 开源仓库 |
| 思澈技术 BBS | https://bbs.sifli.com/ | 官方社区，搜 sf32lb52 / lchspi / 黄山派 |
| gbarecomp 介绍 | https://1379.tech/expanding-the-recomp-ecosystem-with-gbarecomp/ | gbarecomp 技术架构博文 |
