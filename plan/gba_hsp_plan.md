# GBA ROM 重编译到思澈黄山派 实施计划

把 gbarecomp（AOT 静态重编译 + Tier3 解释器兜底）移植到思澈 SF32LB52x 黄山派：Mario Kart Super Circuit 4 MB ROM 已真机运行、可操作，当前处于性能优化阶段。

> 最后更新：2026-11-20

## 0. 当前状态总览

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 0 | 环境 + LCD 点亮（SDK `example/lcd_fb`） | ✅ 完成（CO5300 点亮；arcgcc 14.2.1 / SCons 4.9.1） |
| Phase 1 | PC 端 gbarecomp 重编译 ROM/BIOS → C++20 | ✅ 完成（Mario Kart 4 MB；8 shard；slim 后 `.text` 12.68 MB） |
| Phase 2-A | 仓库根独立 scons 工程骨架 | ✅ 编译通过（`main.bin` 325,240 B） |
| Phase 2-B | 显示后端：中央 240×160 RGB565 | ✅ 完成；当时的「滚动彩条」实为 165×15 坐标 bug，已在 2-C 修复 |
| Phase 2-C | runtime 移植到 SiFli HAL | ✅ 真机验证：BIOS 从 reset vector 跑到空闲态，236 个函数入口，无 fault；送屏几何修复后居中 240×160 |
| Phase 2-D | 真实 ROM 接入：TF 卡读入 PSRAM + cart dispatch | ✅ 2026-11-13 出游戏画面；入口种子 `0x08000000`；9 个 miss PC 由 Tier3 自愈、累计 51 次后收敛，全程无 FATAL |
| Phase 2-E | 容量优化（E1 寄存器提升 / E2 派发表压缩 / E3 去重） | 📋 候选归档，非阻塞（ROM 余 351 KB）；Tier3 已使补种子路线不再必要 |
| Phase 3 | 音频 / 按键 / 存档 | 🔄 按键已跑通（触摸虚拟手柄 Start/A/十字键，11-17 验证）；音频与存档明确不做 |

**当前性能**（11-20，audio 静音后）：轻场景 7.0–8.1 s/60f（≈7.4–8.6 fps）；重场景（4 层 text BG 全屏）10.4 s/60f（≈5.7 fps，静音前 10.9–11.7 s）。目标已由 60 fps 修正为 **7 → 10 fps**——60 fps 在本平台不可达（天花板由 render 的 ≈89 ms/帧决定）。

**烧录**：`build_sf32lb52-lchspi-ulp_hcpu/uart_download.bat`，串口 **COM14**（CH340），三镜像 `ftab@0x12000000` / `bootloader@0x12010000` / `main@0x12020000`。**约定由用户本人执行烧录**，AI 只编译并给出命令。

**ROM 与屏幕**：TF 卡根目录 `/gba.gba`（SPI1，注册 `sd0`，开机整份读入 PSRAM；`bus bind` 924 ms 即 4 MiB 全盘扫描代价，非故障）。GBA 窗口在 390×450 面板上位于 **(75,96)-(314,255)**（240×160），上下两条带子留给虚拟手柄。

## 1. 性能分析结论（最重要）

### 1.1 当前性能画像

稳态 `wall ≈117–135 ms/帧`（7.4–8.6 fps），各项串行相加（11-20 audio 静音后口径；静音前 devtick 为 ~19 ms/帧、887 次 flush/帧）：

| 项 | ms/帧 | 占 wall | 原因（一句话） |
|----|-------|---------|----------------|
| AOT 指令执行 | ~54 | ~42% | 每 guest 指令 300–400 宿主周期：`g_cpu.R[]` 全内存访存 + 派发解析 + 每指令 `runtime_tick` 调用链；codegen 无寄存器分配（Phase 2-E 的 E1） |
| 软件 PPU render | ~43 | ~33% | 逐像素解码：单 BG 层 ~129–204 cyc/px、整屏 38,400 px；其中 text BG3 ~24 ms/帧 |
| devtick | ~12 | ~10% | 静音后只余 timer / SIO / `ppu->tick` 三调用，≈620 次/帧、单次均摊 ≈20 µs |
| 访存 `memtot` 读+写 | ~9 | ~7% | 入口开销（IO 判定 / trace / resync）是真实搬运的 3–6 倍；单次 IWRAM 300–660 cyc，而 PSRAM 基准仅 71 cyc |
| `pres` 送屏 | ~3 | 2% | 每帧只推 76.8 KB，以 convert 为主，已稳定 |
| dma + recfg | ~1.5 | <2% | DMA 0.3 ms/帧、recfg 0.55 ms/帧 |

**devtick 内部（`devX` 探针，11-19 17:40 日志，稳态 ≈1250 ms/60f）**

| 子桶 | ms/60f | 占 devtick | 每 tick 成本 |
|------|--------|-----------|-------------|
| `audio` | 386–404 | ≈32% | ≈1780 cyc（BIOS 段仅 222，进卡带后翻倍——音序器 + 混音） |
| `rest` | 367–373 | ≈30% | ≈1668 cyc（每轮 4 次 `cycles_until_*` + vblank latch + 记账） |
| `ppu` | 265–277 | ≈22% | ≈1235 cyc（仅 `ppu->tick` 事件扫描，不含像素渲染） |
| `io` | 198–216 | ≈17% | ≈960 cyc（`tick_timers + tick_sio`，全程恒定） |

四项和 = tick（误差 ≤1 ms），`flush = render + devtick + dma + recfg` 亦自洽。**devtick 没有单一大头**：audio 与 rest 并列第一、各约 1/3。

**11-20 静音后**（`GBARECOMP_AUDIO_MUTE=y`）：`audio 0`，tick 1258 → **728 ms/60f**（io 168 / ppu 250 / rest 309），flush 调用从 887 → 623 次/帧。flush 次数同步下降是附带收益——事件预算不再被 audio 每采样周期打断，块更大；`tick_devices` 内部仍按事件边界切分，时序不变。

一句话：**瓶颈是「guest 指令跑得太慢（AOT 每指令数百周期）」+「软件渲染太贵（每像素 150+ 周期）」，再叠加一层按 guest 周期摊销的设备 tick 固定成本**。

### 1.2 已排除的假设

| 假设 | 否定依据 |
|------|---------|
| 缓存 / MPU 未生效 | I/D cache 与 MPU 三个使能位全开，9 个 MPU 区域与 SDK `__WEAK` 默认一致；XIP 取指净 8–12 cyc（`fetch bench` net 12.03 / 7.99 cyc）⇒ 取指通路有效。口径限制：仅证明 4 指令热循环命中 I-cache，不证明 13 MB `.text` 跨派发跳转都命中 |
| DMA / HDMA 是 `flush − render` 主因 | dma 17–18 ms/60f（≈0.3 ms/帧）、recfg 46–49 ms/60f（≈0.8 ms/帧），合计 <2% wall；该差额 96% 是 devtick |
| 访存是主因 | 赛道段读写合计只占 `disp` 12–16%（≈7% wall）；即便开 `GBARECOMP_NATIVE_MEMORY` 也只能碰 EWRAM+IWRAM 的 ~6%，入口固定开销仍在 ⇒ 不做 |
| EPIC 2D 硬件加速可救主瓶颈 | 无调色板索引格式（无 CLUT）、无 tilemap 模式、52x 仅 2 普通层 + 1 mask；只能加速 out 与 OBJ，乐观省 10–15 ms/帧，触不动 bg 解码主体 |
| 跳帧是优化方向 | 有效（render 58.8→29 ms/帧，7.2→8.7–10.0 fps），但 11-18 验收后已关闭开关，回到每帧基线以优化 `disp`；加大到 `kRenderEvery=3` 收益递减 |
| `-Oz` 全局编译没问题 | `-Oz` 关掉几乎所有内联启发式，是根因之一；`gba_ppu.cpp` 单文件改 `-O2` 后 bg −18%、out −43%、prep −41%、render −25%、wall −16% |
| Tier3 解释器在热路径 | 稳态 53–167 ms/60f（占 `disp` 0.5–1.5%），最坏 762 ms（7.5%），标题段 9–25 ms ⇒ 不补种子、不改桥 |
| 60 fps 可达 | 天花板由 render 决定：4 层 text BG 重场景 render 5.37 s/60f ⇒ ≈11.2 fps（静音后非渲染开销 85 ms/帧，已不再是主项）；每像素预算 104 cyc/px，实测单 BG 层 129–243 cyc/px ⇒ 当场否决 |
| audio 混音是显著开销 | 关掉后 devtick 只从 1258 降到 728 ms/60f（≈4% wall，重场景 10.9→10.4 s）；audio 只占 devtick 的 32%，且必须同步摘掉 `recompute_event_budget` 里的 audio 约束，否则预算恒 1、帧率反而大跌（11-20 首测已踩，见 §5） |

### 1.3 关键教训

1. **BG run 批量化曾整批回退**（2026-11-17）：4bpp run 快路径上线后画面大面积块状马赛克；nibble 轮转 bug 修好后仍有图形异常，代码层复核判为等价却无法定位 ⇒ 整批回退。由此定纪律：**一次只改一处、单点开关、单点回退**；每个渲染优化先在 PC 端做逐像素等价性验证（同一帧分别用通用/优化循环渲染，比对 `latched_fb_`）再上机。
2. **收益上限要先算**：B-2c（4bpp 行解码批量化）在三场景都生效，但赛道 bg 只占全帧 15%，收益上限仅 2–4% wall，不是主项。
3. **立项评估漏掉三点**（写入后续评估口径）：ASIC 算力不当成免费（软件模拟需各模块相加而非取 max）；对 cache miss / 派发二分零建模；codegen 无寄存器分配。

### 1.4 渲染内部构成

`render` 占 `halt` 的 69–93%（典型 75–85%），差额 0.5–1.4 s/60f 是 HALT 泵本体（3 万–5 万次/60f，折合 17–30 µs/次），属第二小项，暂不动。

| 分段 | 表现 | 结论 |
|------|------|------|
| `bg` | 赛道段 2.07–4.10 s/60f，占 `render` 56–77%，换场景后稳定贴上 ≈4.1 s | 第一大单项；成本由层数/卷动决定，不是随机抖动 |
| `obj` | 92 ms → 2.34 s/60f，随精灵数线性；菜单/logo 段 BG 停摆时反而占 48–74% | 菜单/标题靠 OBJ 主导，赛道几乎为 0 |
| `out` | 稳定 0.39–0.78 s/60f（8–22%） | O(px) 固定成本（RGB888 组装 + 写行），与场景无关，低风险可摘 |
| `prep` | 正常 3–8%，菜单段 854 ms 占 26%，同期 bg 仅 0.39 s | 含与 BG 无关的固定扫描（窗口/OAM/图层状态），BG 停摆时才暴露 |
| `fgn` | 2–20 ms/60f | foreign presentation 不是问题 |

每像素成本：轻载 313 cyc/px、常规 427–565、重载 755 cyc/px，而硬上限是 104 cyc/px。按 PSRAM 随机读 71 cyc 折算相当于每像素 4–10 次随机取数 ⇒ 瓶颈在「逐像素解码 + 每层反复取 tile/调色板 + 逐像素提交」，不是单次访存延迟。`bg` 下层的 text 层在所有场景主导：标题 `r2+r3` 79%、菜单 `r0+r1` 69%（加 affine 为 99%）、赛道 text 四层 64% + affine 34% ⇒ 4bpp 批解码是三场景通用的唯一子项。

**赛道态逐层成本（`bgL`，4 层 text BG 全屏，11-19 日志）**

重场景 wall 10.9–11.7 s/60f（≈5.2–5.5 fps），render 5.35 s/60f 占 wall 48%，其中 bg 4.07 s（四层 text 合计 4.05 s，占 bg 99.5%）。

| 层 | ms/60f | 覆盖 px | cyc/px |
|----|--------|---------|--------|
| r0 | 509 | 575,520 | 212 |
| r1 | 335 | 331,248 | 243 |
| r2 | 1243 | 2,304,000 | 129 |
| r3 | 1960 | 2,304,000 | 204 |

r2/r3 覆盖面积完全相同却差 717 ms/60f（≈6% wall），最可能来自位深/调色板模式分叉 ⇒ 纯读代码可定位、零风险的优化点。

### 1.5 硬上限判据与验收基线

任何「实时」目标先过这三条，任一不过即否决：

1. **每像素预算**：16.74 ms × 240 MHz ÷ 38,400 px = **104 host cyc/px**，须覆盖全部 BG 层 + OBJ + 混合 + 输出（当前单 BG 层 156 cyc/px）。
2. **等效主频**：纯软件扮演 16.78 MHz ARM7 + 全套 ASIC，经验需 ≥1 GHz 级通用核；240 MHz M33 差约一个数量级。
3. **串行惩罚**：真机并行的模块（PPU/DMA/音频）必须单独核算独占时间，不得并入 CPU 预算。

固定三条操作路径做验收（各自 ≥60 帧，日志名与操作路径一并记录）：

| 路径 | 基线 | wall（60 帧） |
|------|------|---------------|
| A 标题（无按键） | 5.3–5.5 fps | 10.8–11.5 s |
| B 选择菜单（按 Start/A） | 3.5–4.5 fps | 13.2–16.9 s |
| C 赛道（选关进入） | 3.6–4.0 fps | 15.0–16.5 s |

## 2. 关键技术与决策

**AOT 静态重编译 + 运行时 Tier3 解释器自愈**
gbarecomp 把 ARM7TDMI 机器码翻成 C++20（`-Oz` 后 8 shard，`.text` 12.68 MB），漏编译的函数不补种子，改由运行时解释器兜底。理由：全 ROM 扫描出 413 个「像真函数」的数据表指针，写进 seeds 实测派发表 +21,593 项、代码 +≈2.6 MB，直接 `region 'ROM' overflowed by 2474764 bytes`（每漏项都是调用图根，会递归带入整片子系统）；而解释器四文件仅 ≈9.6 KB。实测 9 个 distinct miss PC，累计 51 次后不再增长（每个漏项只在首次进入时解释），全程无 FATAL。`overlay_try_dispatch` 恒 0（设备无编译器），`runtime_unimplemented_op` 仍硬停（那是 codegen 缺口，不是发现盲区）。

**为什么不直接 ARM7TDMI → Cortex-M33 翻译**
Layer 1（ARMv4T → C）语义损失近 0；Layer 2（C → Thumb-2）用 gcc 与手写差距 <10%。直接写翻译后端需 4–6 周，且 runtime / self-heal / 可调试性全部要重做；真正的瓶颈是 PPU 软渲染，与指令翻译层无关。

**SLIM 瘦身**
去掉每指令的 `runtime_should_yield()` 与 `g_runtime_insn_trace` 两个检查：`.text` 16.98 → 12.68 MB（−25.4%）。周期计数 `runtime_tick` 仍保留在每条指令，时序精度不变，只是抢占粒度从「每指令」变成「每控制流跳转」。`-Oz` 与 `-Os` 产出完全相同，因此膨胀是生成代码结构问题，不是优化等级问题。

**HALT 泵契约**
`halted=1` 后 `yield` 返回 true 是对的，但宿主循环若无 `pump_idle` ⇒ 无指令执行 ⇒ `runtime_tick` 不前进 ⇒ 到不了 scanline 160 ⇒ 无 VBlank IRQ ⇒ `clear_halt()` 永不执行，形成永久闭环。修复：halted 时宿主按 `min(ppu/io/audio 下一事件)` 推进 `runtime_tick(chunk)`，并在每轮 `present_frame()`（按 `g_runtime_vblank_starts` 去重，一个 VBlank 只刷一次）。契约必须显式写成清单，不能依赖 host runner 的调用位置。

**yield 粒度结论**
`yield` 混着两类：采样/检查类（栈 guard、断点、trace、watchdog、PC 采样）与契约类（present、halt 交还）。SLIM 删掉每指令调用合理——设备无调度器可切，采样类本来就 inert；真正的失误是没把「卡带无 yield 点 ⇒ 出帧/HALT 推进/栈水位由宿主负责」写成显式契约。契约类需求只有 60 Hz 级，与每指令相差约 20 万倍，故按「钩子需要的触发频率」分层，不采用「保留 yield 只删采样类」（成本主体是不可内联的每指令调用）。SLIM 下 yield 无法级联展开，栈保护若真需要必须是 codegen 新增回边/基本块级插入点。

**显存与内存放置**
VRAM/OAM/palette 每像素被采样，放片上 SRAM（PSRAM 往返 ~70 cyc 且会驱逐 D-Cache）；实测这批缓冲搬到片上的收益只有 0.3–1.2% 且曾伴随画面异常，故布局保留但不再作为优化方向。机器对象 `GbaBus`/`GbaPpu`（~386 KB）与音频 capture ring 放 PSRAM arena——片上 SRAM 装不下 GbaBus + 两帧 ≈616 KB。

**渲染后端**
把 GBA 窗口本身注册成 240×160 RGB565 帧存（`area=(75,96)-(314,255)`、`line_bytes=480`），每帧做 RGB888→RGB565 转换后只推 76.8 KB，而不是整屏 351 KB；全屏帧存只用于开机一次性画机身 + 虚拟手柄。原因：`drv_lcd_fb_write_send()` 的 `dst_line_addr` 不含 x 偏移、按 `fb.area` 整块刷新，**不支持**往全屏帧存写局部 x 区间，否则只会得到 165×15 的交集窗口。

**时间后端**
DWT `CYCCNT` 64 位累加。240 MHz 下 32 位计数 17.9 s 就回绕，曾使 `wall` 与大于 4.29 G 的桶少报 17,895 ms，现 `disp/halt/pres` 全部改 64 位并保留 `disp+halt+pres ≈ wall` 交叉校验。

**派发解析**
`runtime_dispatch` 先查 `kCachedBlock`，未命中再二分 167,911 项派发表（12 B/条约 2 MB，全部常驻 NOR XIP）。ARMv4T 下每个基本块末尾都是跳转，因此**每条 guest 指令都走一次派发**，块间跳转平均约 18 次随机读，二分表常驻 L1 几乎不可能——这是「每指令 300–400 周期」中除 `g_cpu.R[]` 访存外的第二项，也是 E1/E2 的立项依据。

**输入路径（虚拟手柄 + 触摸）**
放弃「KEY1/KEY2 短按长按凑 A/B/Start」（做不出十字键和 L/R），改为整块面板当手柄：GBA 窗口上移，上下两条带子画控件，命中后写 guest `KEYINPUT`。

| 控件 | 几何（面板像素） |
|------|------------------|
| 十字键 | 外框中心 (72,353) 边长 ±61，内十字臂半宽 28，死区 ±19（方形框便于搓对角） |
| A / B | (330,334) r≈30 / (262,384) r≈25 |
| Select / Start | (178,329) r=16 / (178,377) r=16 |
| L / R | 胶囊 (18,28)-(95,65) / (294,28)-(371,65) |

口径：`KEYINPUT` 低 10 位有效、**低电平为按下**，`gba_pad_hit()` 返回按下掩码后写 `(~pressed) & 0x03FF`。实测约束：命中尺度必须与绘制同源（`gba_pad.c` 的布局常量 + ready 标志），否则 A 键判定会偏移到右下方；FT6146 滑动时重复上报 `DOWN` 而非 `MOVE`，两者必须走同一分支；按下高亮需要「面板级 fb + 局部 dirty rect」，暂不做。首版曾用「命中框 = 绘制框 ×25/16 向外扩」，导致 `Select` 按住时同时命中 `B`，已改为直接复用绘制矩形。

## 3. 硬件与环境

### 3.1 板级参数

| 项目 | 值 |
|------|-----|
| 芯片 | SF32LB525UC6（合封 8 MB OPI-PSRAM + 16 MB NOR） |
| 大核 | ARM Cortex-M33 @ 240 MHz（HCPU，RT-Thread） |
| 片上 SRAM | 512 KB（HCPU 专用） |
| 屏幕 | CO5300AF-01 AMOLED 390×450，QSPI RGB565 |
| 触摸 / 实体键 | FT6146（I2C）；KEY1=PA34、KEY2=PA43（active-high） |
| Board | `sf32lb52-lchspi-ulp`（`CHIP=SF32LB52X`、`JLINK_DEVICE=SF32LB52X_NOR`、`CORE=HCPU`） |
| SDK | `E:\work\gitee\sifli\SiFli-SDK\v2.5.1`（只用 SDK，不引入 Solution） |

### 3.2 内存布局要点

```
片上 SRAM 512KB：rt-thread + SDK 栈堆 ~100KB、固件 RAM 228,700/523,264 B（含 32 KB 主线程栈）
  └ render_scanline_impl 栈帧 12,428 B ⇒ 默认 8 KB 栈会 STKOF 硬故障，故 32 KB
OPI-PSRAM 8MB：g_gba_arena（GbaBus+GbaPpu）1MB 预留 + 冷缓冲 arena 1MB
  + LCD 全屏帧存 390×450×2 ~343KB + GBA ROM 运行期镜像 4MB ⇒ 合计 ~6.4/8MB
NOR 16MB：ftab 32KB + bootloader 64KB + main 分区 0xFD8000 ≈15.84MiB（工程 ptab 覆盖板级 8MiB 上限，删 dfu/fs_root）+ kvdb
TF 卡 SPI1（CLK PA28/MOSI PA24/MISO PA25/CS PA29/CD PA27，上限 12MHz），ROM 固定 /gba.gba
```

PSRAM 只能承载运行期数据（BSS + 自己清零/拷贝），**不能**放链接期带初值的 const 表。ROM 原始字节必须常驻 RAM（图形/音频/关卡数据运行时仍经 `0x08000000` 读取），因此开机整份读入，而不是按需读卡（SPI 12 MHz ≈1.2 MB/s，每帧仅 ~20 KB 预算，同步 bus 读会阻塞 DMA/IRQ）。

### 3.3 构建命令（PowerShell）

必须照 `set_env.bat gcc` 组装 PATH，缺 `tools\bin` 会在生成 `link_copy.lds` 时报 `The system cannot find the file specified`：

```powershell
$env:SIFLI_SDK='E:/work/gitee/sifli/SiFli-SDK/v2.5.1'; $env:SIFLI_SDK_PATH=$env:SIFLI_SDK
$env:RTT_CC='gcc'; $env:RTT_EXEC_PATH='D:\sifli\env\tools\gnu_gcc\arm_gcc_14.2.1\bin'
$env:PYTHONHOME='D:\sifli\env\tools\python-3.11.9-amd64'
$env:PYTHONPATH="D:\sifli\env\tools\python-3.11.9-amd64;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\build;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\build\default"
$env:PATH="D:\sifli\env\tools\git\Git\bin;D:\sifli\env\tools\bin;$env:RTT_EXEC_PATH;$env:PYTHONHOME;$env:PYTHONHOME\Scripts;D:\sifli\env\tools\qemu\qemu32;D:\sifli\env\tools\cmake\bin;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\menuconfig\dist;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\scripts;" + $env:PATH
scons --board=sf32lb52-lchspi-ulp -j8
```

坑：直接调 `scons` 可能命中 Python 2.7 的 `scons.bat`（`site.py` SyntaxError），`Scripts\scons.exe` 全路径调会 `SRE module mismatch`；按上面 PATH 注入后用 `scons` 即可，或 `python.exe -m SCons`。路径盘符必须大写 `E:`（小写会让 board 源文件重复加入，报 `multiple definition of bsp_init`）。根目录 `.sconsign.dblite` 丢失会触发**全量重编译**（重建即可，不要去恢复回收站）。

链接期注意：`rtconfig.py` 必须存在（空文件亦可），否则 `LINK_SCRIPT_SRC` 为 None 抛 `AttributeError`；本 SDK 的 `drv_lcd_fb_send()` 只有声明没有实现，清屏要用 `drv_lcd_fb_write_send()`。

**烧录命令**（`uart_download.bat` 内部等价形式；失败时报 `Failed to connect to the chip` 通常是板子未进入 UART 下载模式，不是工具问题）：

```
sftool -p COM14 -c SF32LB52 -m nor write_flash "bootloader\bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab\ftab.bin@0x12000000"
```

PC 端重编译：VS DevShell + Ninja 构建 `gba_recompile`（cmake 3.30 无 VS2026 生成器），必须带 `/utf-8`（否则中文注释吃掉枚举名报 C2838），三个子模块必须 checkout 到 pin 的 commit（用 master 缺 `ArmCodegen::supports_native_region`，报 C2039）。

### 3.4 当前镜像尺寸

`main.bin` **16,371,540 B**，ROM 分区余 **351,444 B**，RAM **228,700 / 523,264 B**；仅剩既有的 `g_force_interp` 与 RWX 段告警。symbol_map.cpp（519 KB）不链接，串口 trace 只显示地址（2-E 若要恢复函数名，用区间编码压到 ~120 KB）。

## 4. 已完成能力清单

- BIOS 从 reset vector 静态重编译执行至空闲态（236 个函数入口，无 fault）。
- 真实 ROM 4 MB 从 TF 卡 `/gba.gba` 整份载入 PSRAM，cart dispatch 联动。
- 派发链路完整：`runtime_dispatch` → 派发表 → shard 函数；miss 由 Tier3 解释器自愈。
- 送屏：240×160 RGB565 居中，每帧只刷窗口帧存（76.8 KB）。
- 虚拟手柄触摸输入：Start / A / 十字键均实测驱动游戏（进菜单、确认、移动）。
- 逐步小改的三组实验有明确结论：`PPU_BG_ZERO_RUN`（全透明 run 跳过）有效，保留 default y；`PPU_BG_RUN_SKIP` 无效（不透明像素 37.6%，8 px run 全被覆盖概率 ≈0.04%）；`PPU_RESTRICT_BUFFERS`（`__restrict`）零收益，已删除。
- 移植面已收敛：只复用纯逻辑层（`src/gba/*.cpp`、`runtime_arm.cpp`、`runtime_bus_bridge.cpp`），不链接 `runtime.cpp`（`std::thread` 主循环）、`host_window.cpp`（SDL）及 launcher/overlay/mod/multiplayer/tcp_debug 等宿主层；全局加 `-std=c++20 -fexceptions -funwind-tables`（runtime 用 `throw` 退出 dispatch，SDK 默认 `-fno-exceptions`）。
- 6 个 gbarecomp 适配宏已变成 Kconfig 选项（`menu "GBARECOMP runtime"`，`scons --menuconfig` 开关），构建时打印 `[gba] runtime features`。主要三项：帧存改 `std::array` 240×160×3（替代 1280×864×3 堆缓冲）、去掉 `<thread>` 采样线程、trace ring 4096→64 条 + 去 hang dump 各省约 288 KB。
- BIOS 二进制在 scons 阶段转成 C 数组，免去运行时文件系统；主线程栈必须 ≥32 KB（`render_scanline_impl` 栈帧 12,428 B，8 KB 会 `STKOF` 硬故障），栈从堆分配，静态 RAM 占用不变。

## 5. 待办与风险

**待办（按优先级）**

1. `devX` 探针**已实测**（见 §1.1）：devtick 无单一大头，audio ≈32%、rest ≈30%、ppu ≈22%、io ≈17%。
2. **audio 静音已生效（11-20 复测通过）**：`GBARECOMP_AUDIO_MUTE` 关闭 `audio().tick`、`tick_devices` 采样约束、HALT 泵采样事件，以及 `recompute_event_budget()` 里的 audio 预算源（首轮漏掉最后一处，导致预算恒 1、flush 暴涨 25×、帧率反降，已修）。同场景（4 层 text BG）实测：devtick 1258 → 728 ms、flush 53,218 → 37,379 次、recfg 56 → 33 ms、wall 10.9–11.7 → 10.4 s/60f（≈5.4 → 5.7 fps）。flush 次数同步下降是因为预算不再被每采样周期打断，块更大——`tick_devices` 内仍按事件边界切分，时序不变。后续如需再省，可查 HALT 泵（占 wall 61%，其中大部分是 render）。
3. 若 audio 收益确认，再合并 `rest` 里的 `cycles_until_*` 查询（≈1.7%）；`ppu`/`io` 各 <5 ms/帧，不投入。
4. 查 r3 vs r2 的 204 vs 129 cyc/px 差异（收益上限 6% wall，纯读代码）。
5. B-2c：4bpp BG run 批解码（收益 2–4% wall，需先做 PC 端逐像素等价性验证）。
6. Phase 2-E E1 寄存器提升——唯一能压 AOT（~42%）的方向，未实施（改 codegen + 重新生成 ROM + 全量回归，成本高）。
7. 补验按键 B / Select / L / R（11-17 日志未覆盖），以及 KEY1/KEY2 → GBA 按键的备份映射。
8. 三条固定验收路径（标题 / 选择菜单 / 赛道）需在明确按键操作下重取对照日志；无按键只有标题画面。

**明确不在范围**：音频输出、存档（依据「性能不达标，音频和存档没有意义」）。按下高亮需要「面板级 fb + 局部 dirty rect」，暂不做。

**风险**

- `third_party/gbarecomp-main` 不受 git 管理（被 `.gitignore` 忽略），其改动只存在于工作区；动 `gba_ppu.cpp` 等文件前必须整体快照，否则无法精确回退。
- `patches/0001-gbarecomp-sf32-hsp-adaptations.patch` 为累计补丁（针对 `third_party/gbarecomp.tar.gz` 的 `src/` 子树），**11-20 已重新导出并 `git apply --reverse --check` 全绿**（覆盖 9 个文件，bridge 145 行 / ppu 379 行）。改 third_party 后须按 `tar 解压 → git diff --no-index --no-prefix a/… b/…` 重导并跑一次反向校验，否则补丁会再次失真。
- 性能目标（7→10 fps）尚未覆盖最慢场景：主界面重 bg 态 wall 10.4 s/60f（≈5.7 fps，audio 静音后），验收应以它为下限。
- 游戏 ROM > 8 MB 时 NOR 装不下，必须走 TF 卡按需加载（当前 4 MB 走全量读入 PSRAM 的简单路径）。
- gbarecomp 对目标 ROM 覆盖率不足（fallback 太多）为中等风险，先用已重编译验证过的 P0/P1 游戏；manifest 反馈再补。
- 已消除的旧风险：颜色格式色偏（present 里 RGB888→RGB565，多轮真机确认正常）；触摸手柄绘制/命中错位（命中判定复用 `gba_pad_draw()` 的 layout 常量）。

## 6. 参考文件速查

**本地 SDK**

- `E:\work\gitee\sifli\SiFli-SDK\v2.5.1\` — SDK 根，只用 SDK 不引入 Solution；`set_env.bat` 组装构建环境。
- `customer/boards/sf32lb52-lchspi-ulp/hcpu/board.conf` — 板级配置，LCD / 触摸 / 音频 / KEY1/KEY2 已预配。
- `drivers/Include/bf0_hal_*.h` + `drivers/hal/bf0_hal_*.c` — HAL 唯一权威（LCDC / AUDCODEC / GPIO / DMA / EPIC）。
- `middleware/rt-thread/components/drivers/` — `drv_lcd_fb`（帧缓冲抽象）、`drv_epic`（ePicasso）。

**参考 Example**

- `example/lcd_fb/` — 帧缓冲初始化 + 双缓冲 + fill，2-B/2-C 的模板。
- `example/lcd_stress_test/` — LCD 基础操作。
- `example/hal_example/` — HAL 各外设用法。
- `example/multimedia/audio/dual_adc_dac/`、`local_music/` — AUDCODEC DAC 配置与 DMA 播放（音频未实施）。

**重编译**

- https://github.com/mstan/gbarecomp — 引擎（唯一公开的 GBA ARM7TDMI → C 静态重编译工具）。
- FireRedLeafGreenRecomp / EmeraldRecomp / MinishCapRecomp — 已验证的重编译工程，可参考 runtime 设计。

**在线文档**

- https://docs.sifli.com/projects/sdk/latest/sf32lb52x/ — SDK 级文档。
- https://github.com/OpenSiFli/ — SDK + BSP 开源仓库；https://bbs.sifli.com/ — 官方社区。
- https://1379.tech/expanding-the-recomp-ecosystem-with-gbarecomp/ — gbarecomp 技术架构博文。

## 7. 里程碑

| 日期 | 事件 |
|------|------|
| 2026-11-10 | 环境就绪 + LCD 点亮 + 首烧成功（sftool / COM14） |
| 2026-11-11 | Phase 2-C 复烧确认居中 240×160（修 165×15 坐标 bug） |
| 2026-11-13 | 真实 ROM 出游戏画面，5.0 fps；Tier3 自愈闭合 51 次 miss |
| 2026-11-16 | `gba_ppu.cpp` 单文件 `-O2`，7.2 fps（−16% wall） |
| 2026-11-17 | 按键可用（虚拟手柄）；BG run 优化整批回退，定纪律；60 fps 结论修正为 7→10 fps |
| 2026-11-18 | 跳帧验收（8.7–10.0 fps）后关闭；访存归因（仅占 `disp` 12–16%）；B 分段出数 |
| 2026-11-19 | 缓存/XIP 与 DMA 假设否定；`devX` 实测：devtick 均摊 audio 32% / rest 30% / ppu 22% / io 17%；重场景 5.2–5.5 fps，r3 解码异常 |
| 2026-11-20 | audio 静音首测：audio=0 成立，但 `recompute_event_budget` 漏改导致预算恒 1、flush 暴涨、帧率反降；修复后复测 devtick 1258→728 ms、flush 53k→37k、同场景 wall 10.4 s/60f（≈5.7 fps） |
