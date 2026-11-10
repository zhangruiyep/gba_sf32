# GBA ROM 重编译到思澈黄山派 实施计划

> 最后更新：2026-11-10

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
├── rt-thread + SDK 栈堆       ~100KB
├── GBA 帧缓冲 240×160×2       = 76.8KB
├── GBA 全局状态/WRAM+EWRAM    ~320KB  (CPU状态 + 32KB WRAM + 256KB EWRAM + 其他)
└── 剩余                        ~15KB

OPI-PSRAM 8MB (0x60000000):
├── GBA ROM 镜像 (.gba)          1-8MB  (Phase 1 先用 <8MB 游戏)
├── 重编译 C 代码 + 只读数据     ~1-4MB
└── 剩余空间

NOR Flash 16MB (0x00200000):
├── bootloader + SDK 固件        ~200KB
└── 应用固件                     剩余
```

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

### Phase 1：PC 端 — gbarecomp 重编译产出 C 代码（1 天）

> **为什么不跳过**：gbarecomp 是 Rust 命令行工具，必须在 PC 上跑（目标板 Cortex-M33 没有 Rust 工具链）。这一步只是"在 PC 上跑工具生成代码"，**不是在 PC 上运行模拟器**。
>
> **砍掉了什么**：原版 Phase 1 的"在 PC 上编译运行 + 替换 SDL 后端为 headless stub" — 这部分跳过。SDL 后端直接替换为思澈 SDK HAL 后端（Phase 2 做）。

**目标**：把 Pokemon FireRed ROM 重编译成 C 代码产物

**步骤**：
1. 安装 Rust：`rustup default stable`
2. `git clone https://github.com/mstan/gbarecomp.git` + 依赖仓库
3. `cargo build --release` 构建 gbarecomp CLI
4. 准备合法的 GBA BIOS dump (`gba_bios.bin`) 和 ROM (`PokemonFireRed.gba`, ~5MB, SHA-1: `41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`)
5. 执行重编译（参考 FireRedLeafGreenRecomp README）:
   ```
   gba_recompile --rom PokemonFireRed.gba \
                 --config variants/firered/symbols/firered.toml \
                 --out variants/firered/generated
   ```
6. **分析输出**（必须做，这是决定后续移植策略的关键）:
   - 产物目录结构：generated/src/ 下有哪些 C 文件？game 代码 + runtime 分别在哪？
   - C 还是 C++？（文件后缀是 .c 还是 .cpp？有没有用到 C++ 特性？）
   - 有没有 Cortex-M33 不兼容代码？（内联汇编、编译器特定 attribute、SWP/CP15 指令）
   - 代码总量：C 文件行数、文件数量、总大小（决定 PSRAM/Flash 分配）
   - 有没有依赖 C++ STL？（如果有需要在 SDK scons 里开 C++ 支持）
7. 把产物拷贝到 `e:\work\Github\gba_sf32\src\gbarecomp\`

### Phase 2：思澈平台移植 — CPU + LCD（4-6 天）

**目标**：在黄山派上跑通重编译的 CPU + 画面

**工程位置**：在当前项目目录下，scons 工程独立于 SDK（SDK 路径通过 SConstruct 中的变量配置）：

```
e:\work\Github\gba_sf32\              # 当前工程根（独立于 SDK）
├── SConstruct                         # scons 入口（配置 SDK_ROOT = "e:/work/gitee/sifli/SiFli-SDK/v2.5.1"）
├── SConscript
├── sdk_conf/                          # scons 配置文件
│   ├── Kconfig.proj
│   ├── rtconfig.py                    # board = 'sf32lb52-lchspi-ulp'
│   └── link.ld                        # 链接脚本（调整内存布局）
├── src/
│   ├── main.c                         # 应用入口
│   ├── gba_config.h                   # GBA 内存/ROM 地址配置
│   ├── hal_port/                      # SDK HAL 适配层
│   │   ├── render_backend.c           # LCDC + 帧缓冲 blit
│   │   ├── audio_backend.c            # AUDCODEC DAC + DMA
│   │   ├── input_backend.c            # GPIO KEY1/KEY2
│   │   ├── time_backend.c             # Cortex-M33 DWT cycle counter
│   │   └── storage_backend.c           # HAL FLASH 读写
│   └── gbarecomp/                     # Phase 1 产出的 C 代码（整份拷贝）
│       ├── bios/
│       ├── game/
│       └── runtime/
└── plan/gba_hsp_plan.md

构建命令: cd e:\work\Github\gba_sf32 && scons --board=sf32lb52-lchspi-ulp -j8
```

**SConstruct 中的 SDK 路径配置**（示意）：
```python
# SConstruct 顶部
SDK_ROOT = os.environ.get('SIFLI_SDK_ROOT', 'e:/work/gitee/sifli/SiFli-SDK/v2.5.1')
# 后续引用 SDK 下的 drivers、rtos、customer/boards 等路径
```

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

| # | 验证点 | 通过标准 |
|---|--------|---------|
| 2.1 | LCD 屏幕点亮 | 全黑背景 390×450 |
| 2.2 | 居中区域可写 | 中央 240×160 显示测试色块 |
| 2.3 | CPU 跑通 BIOS | 串口打印 BIOS 初始化日志 |
| 2.4 | PPU 软渲染输出 | GBA 画面正确显示在居中区域 |
| 2.5 | 颜色格式正确 | BGR555 → RGB565 颜色无偏差 |
| 2.6 | 跑通游戏标题画面 | Pokemon FireRed 标题显示 |

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
