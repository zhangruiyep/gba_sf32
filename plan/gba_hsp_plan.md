# GBA ROM 重编译到思澈黄山派 实施计划

把 gbarecomp（AOT 静态重编译 + Tier3 解释器兜底）移植到思澈 SF32LB52x 黄山派：Mario Kart Super Circuit 4 MB ROM 已真机运行、可操作，当前处于性能优化阶段。

> 最后更新：2026-11-21

## 0. 当前状态总览

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 0 | 环境 + LCD 点亮（SDK `example/lcd_fb`） | ✅ 完成（CO5300 点亮；arcgcc 14.2.1 / SCons 4.9.1） |
| Phase 1 | PC 端 gbarecomp 重编译 ROM/BIOS → C++20 | ✅ 完成（Mario Kart 4 MB；8 shard；slim 后 `.text` 12.68 MB） |
| Phase 2-A | 仓库根独立 scons 工程骨架 | ✅ 编译通过（`main.bin` 325,240 B） |
| Phase 2-B | 显示后端：中央 240×160 RGB565 | ✅ 完成；当时的「滚动彩条」实为 165×15 坐标 bug，已在 2-C 修复 |
| Phase 2-C | runtime 移植到 SiFli HAL | ✅ 真机验证：BIOS 从 reset vector 跑到空闲态，236 个函数入口，无 fault；送屏几何修复后居中 240×160 |
| Phase 2-D | 真实 ROM 接入：TF 卡读入 PSRAM + cart dispatch | ✅ 2026-11-13 出游戏画面；入口种子 `0x08000000`；9 个 miss PC 由 Tier3 自愈、累计 51 次后收敛，全程无 FATAL |
| Phase 2-E | 容量优化（E1 寄存器提升 / E2 派发表压缩 / E3 去重） | 📋 候选归档，非阻塞（ROM 余 ≈235 KiB）；Tier3 已使补种子路线不再必要 |
| Phase 3 | 音频 / 按键 / 存档 | 🔄 按键已跑通（触摸虚拟手柄 Start/A/十字键，11-17 验证）；音频与存档明确不做 |
| Phase 4 | 性能优化 | 🔄 devtick 已收口（audio 静音，1258 → 728 ms/60f）；N1 探针已定论（C 段占 `bg` 78–90%），N3a（8bpp 零 run 跳过）、N3b（逐像素体瘦身，8bpp 层 C −27.7%）均已上机验证无回归；`haltX` 定论后 N2 取消、N5（AOT 每指令成本）转正；N5 的寄存器提升/强制内联因 ROM 只剩 ≈235 KiB 被实测否决，改为 callee 内部瘦身；第 1 步 `GBARECOMP_BUS_FASTPATH`（RAM/ROM 访存入口短路）已上机验证：`memtot` 单次 ≈490→430 cyc，赛道 +2–4% fps，保留；第 2 步 `GBARECOMP_TICK_LEAN`（`runtime_tick` 内部瘦身）已上机验证：赛道均值 15.3 → 14.65 s/60f（≈+4% fps），标题/菜单无回归，保留，见 §5.2 |

**当前性能**（11-21 `haltX` 口径）：轻场景 7.0–8.2 s/60f（≈7.3–8.6 fps）；重场景（4 层 text BG 全屏 / WIN0+WIN1+OBJWIN）10.4 s/60f（≈5.8 fps，静音前 10.9–11.7 s）；赛道段基线 15.7–17.1 s/60f（**3.5–3.8 fps**），N5-1 后 14.8–17.9 s/60f（前 5 块均值 15.3，**3.4–4.1 fps**），N5-2 后赛道稳态 14.3–15.0 s/60f（均值 14.65，**4.0–4.2 fps**）；标题 6.3–9.3 s/60f（6.5–9.6 fps）、标题菜单 6.8–10.5 s/60f（5.7–8.8 fps）、选择菜单（3D OBJ）11.3–16.6 s/60f（3.6–5.3 fps，render 占 49%），构成为 **AOT 63% / render 30% / pres 1%**。目标已由 60 fps 修正为 **7 → 10 fps**——60 fps 在本平台不可达（天花板由 render 的 ≈89 ms/帧决定）。

**本轮改动状态**（2026-11-21 10:06 固件）：`gba_ppu.cpp` 的 N3a 8bpp 零 run 分支与 N3b 逐像素体瘦身（`GBARECOMP_PPU_BG_PIXEL_LEAN`，`Binary size 16,372,156 B`）均已上机：8bpp 层 C 1446 → 1046（−27.7%），4bpp 无回归；行级窗口 mask 在 `7f40` 实测不命中（收益全来自指针提升 + 零索引），原 25% 目标作废，详见 §5.2。`haltX` 探针（`16,372,468 B`）已上机并定论：赛道 `halt` 的 33% 是 render 借道，泵本体 `body` 仅 3.5% wall、`scan` 0.05% ⇒ **N2 取消、N5 转正**，详见 §5.2。随后尝试的 `GBARECOMP_AOT_INLINE_FLAGS`（`cpsr_*`/`arm_set_nz*` 强制内联）链接溢出 1,577,852 B，已完整回退（Kconfig.proj / SConscript / runtime_arm.h / runtime_arm.cpp），工作区回到 `7e6c1b5` + 基线固件 `16,372,468 B`；结论已写入 N5 段。N5-1 固件 `16,372,988 B` 已上机（`GBARECOMP_BUS_FASTPATH`），赛道 14.8–17.9 s/60f，详见 §5.2。当前固件 `16,372,980 B` 为 N5-2 `GBARECOMP_TICK_LEAN`（`runtime_tick` 去掉 `cyc_probe` / `drain_dma_steal` / `irq_pending` 三处外调），已于 11-21 15:09–15:14 上机：赛道稳态均值 14.65 s/60f（≈+4% fps）、标题/菜单无回归，保留，详见 §5.2。

**烧录**：`build_sf32lb52-lchspi-ulp_hcpu/uart_download.bat`，串口 **COM14**（CH340），三镜像 `ftab@0x12000000` / `bootloader@0x12010000` / `main@0x12020000`。**约定由用户本人执行烧录**，AI 只编译并给出命令。

**ROM 与屏幕**：TF 卡根目录 `/gba.gba`（SPI1，注册 `sd0`，开机整份读入 PSRAM；`bus bind` 924 ms 即 4 MiB 全盘扫描代价，非故障）。GBA 窗口在 390×450 面板上位于 **(75,96)-(314,255)**（240×160），上下两条带子留给虚拟手柄。

## 1. 性能分析结论（最重要）

### 1.1 当前性能画像

轻场景稳态 `wall ≈117–135 ms/帧`（7.4–8.6 fps），各项串行相加（11-20 audio 静音后口径；静音前 devtick 为 ~19 ms/帧、887 次 flush/帧）：

| 项 | ms/帧 | 占 wall | 原因（一句话） |
|----|-------|---------|----------------|
| AOT 指令执行 | ~54 | ~42% | 每 guest 指令 300–400 宿主周期：`g_cpu.R[]` 全内存访存 + 派发解析 + 每指令 `runtime_tick` 调用链；codegen 无寄存器分配（Phase 2-E 的 E1） |
| 软件 PPU render | ~43 | ~33% | 逐像素解码：单 BG 层 ~129–204 cyc/px、整屏 38,400 px；其中 text BG3 ~24 ms/帧 |
| devtick | ~12 | ~10% | 静音后只余 timer / SIO / `ppu->tick` 三调用，≈620 次/帧、单次均摊 ≈20 µs |
| 访存 `memtot` 读+写 | ~9 | ~7% | 入口开销（IO 判定 / trace / resync）是真实搬运的 3–6 倍；单次 IWRAM 300–660 cyc，而 PSRAM 基准仅 71 cyc |
| `pres` 送屏 | ~3 | 2% | 每帧只推 76.8 KB，以 convert 为主，已稳定 |
| dma + recfg | ~1.5 | <2% | DMA 0.3 ms/帧、recfg 0.55 ms/帧 |

重场景（4 层 text BG 全屏）同一套探针：**wall 174.5 ms/帧（5.7 fps）** = AOT 65.1（37%）+ render 89.5（51%）+ devtick 12.1（7%）+ memtot 11.4（6.5%）+ pres 3.0（2%）+ dma/recfg 0.8。**render 在重场景反超 AOT**，这是下一轮优先攻 render 的依据（§5.2 N1）。

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

重场景（4 层 text BG 全屏）wall **10.4–11.7 s/60f**（≈5.2–5.7 fps，下限为 11-20 静音后），render 5.37 s/60f 占 wall ≈46–51%，其中 bg 4.07 s（四层 text 合计 4.05 s，占 bg 99.5%）。

| 层 | ms/60f | 覆盖 px | cyc/px |
|----|--------|---------|--------|
| r0 | 509 | 575,520 | 212 |
| r1 | 335 | 331,248 | 243 |
| r2 | 1243 | 2,304,000 | 129 |
| r3 | 1960 | 2,304,000 | 204 |

r2/r3 覆盖面积完全相同，比值却稳定在 **1.6–1.7×**（11-19：129 vs 204；11-20 两帧：89 vs 150、130 vs 205 cyc/px）。比值跨帧一致、绝对值随内容浮动 ⇒ 是**结构性分叉**（位深 / 调色板 / 卷动模式 / 窗口），不是画面内容差异；差值 717 ms/60f ≈6% wall。纯读代码 + PC 端重放即可定位，零风险，列为下一轮第一项。

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

`main.bin` **16,372,988 B**（2026-11-21 N5-1 固件；`.text 16,360,356 + .data 12,628 = 16,372,984 B`），ROM 分区 `0x00FD8000 = 16,613,376 B` ⇒ **余量 240,392 B（≈235 KiB）**；RAM **228,700 / 523,264 B**；仅剩既有的 `g_force_interp` 与 RWX 段告警。symbol_map.cpp（519 KB）不链接，串口 trace 只显示地址（2-E 若要恢复函数名，用区间编码压到 ~120 KB）。

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

### 5.1 已收口（不再投入）

| 项 | 结论 |
|----|------|
| `devX` 探针 | devtick 无单一大头：audio 32% / rest 30% / ppu 22% / io 17%（§1.1） |
| audio 静音 `GBARECOMP_AUDIO_MUTE` | 11-20 复测生效：devtick 1258 → **728 ms/60f**、flush 53,218 → 37,379 次、recfg 56 → 33 ms、同场景 wall 10.9–11.7 → **10.4 s/60f**（≈5.4 → 5.7 fps）。**四个改动点必须同时生效**：`tick_devices` 的采样约束、`audio().tick()` 调用、HALT 泵采样事件、`recompute_event_budget()` 的 audio 查询。首测漏掉最后一处 ⇒ 预算恒 1、flush 暴涨 25×、帧率反降，已修并固化 |
| flush 次数下降（887 → 623 次/帧） | 预算不再被每采样周期打断，块变大；`tick_devices` 内仍按事件边界切分，时序不变 |
| `ppu` / `io` 两个 devtick 子桶 | 各 <5 ms/帧，不投入 |

### 5.2 下一轮执行计划

纪律沿用 §1.3：**一次只改一处、单点开关、单点回退**；渲染类改动先在 PC 端做逐像素等价性验证（同帧分别用通用/优化循环渲染，比对 `latched_fb_`）；每步取同一路径 ≥60 帧日志，对照 `wall / disp / halt / render / devtick / flush`。

**N1 定位 r2/r3 解码分叉**（✅ 探针实测完成，2026-11-20 12:23 日志；上限 6% wall）

稳态窗口（60 帧，`render` 3323 ms / `bg` 2520 ms，`out` 521；288,000 runs/层 = 160 行 × 30 run × 60 帧）：

| 层 | 位深 | `bgL` ms | covered px | opaque | skip | `bgSeg` A/B/C (cyc/run) | 有效 run | C cyc/px |
|----|------|---------|-----------|--------|------|------------------------|---------|----------|
| r1 | 4bpp | 342 | 331,248 | 300,642 | 246,594 | 2 / 62 / **1141** | 41,406 | **143** |
| r2 | 8bpp | 541 | 2,304,000 | **0** | 0 | 2 / 43 / **347** | 288,000 | **43** |
| r3 | 4bpp | 1615 | 2,304,000 | 2,304,000 | 0 | 2 / 66 / **1217** | 288,000 | **152** |

（另窗口 r2 为 4bpp、部分不透明：`bgL` 720 ms、skip 176,298/296,320、`bgSeg` 2/69/454 → 有效 run 120,022、7.7 px/run、**145 cyc/px**。）

1. **C 段（逐像素体）占 `bg` 的 78–90%**：r3 为 1217/1346 cyc/run，是唯一主项；A 段 2 cyc（D-cache 命中）、B 段 56–69（≈一次 `vram` 未命中）、run 级记账 57–61 cyc/run。
2. **与层/尺寸/优先级无关**：同一条 4bpp 代码路径实测 143 与 152 cyc/px，差值只由不透明度决定；`BGCNT`（r3 `0x070b` 4bpp / 256×256 / prio 3，r1/r2 同为 4bpp）已排除「8bpp 缺特化路径」的原假设。
3. **成本构成可由全透明 8bpp 层反推**：43 cyc/px 只走「取索引 + 透明判定 + 循环骨架」⇒ 剩下约 100–110 cyc/px 全在「片内调色板取数 + 窗口/混合判定 + `submit`（12 B 候选插入）」。`membench` 佐证：`vram` 56 / `pal`(PSRAM) 10 / `linepal`(片内) 10 / 候选行 12 ⇒ **内存层级不是主因**，152 cyc/px 无法用取数延迟解释。
4. 反汇编（`render_regular_bg<false,false>` lambda，`.o` 反查）：内层体约 60 条指令，`vram`/`pal`/候选行/窗口行**每像素都经闭包双重解引用重读一次**，`submit` 未内联（`.part.0` 0x190），命中时两次 12 B `ldmia/stmia` 复制。即 152 cyc/px ≈ 60 条指令 + 载入依赖停顿（≈2.5 cyc/指令）。
5. **新发现的独立浪费**：`GBARECOMP_PPU_BG_ZERO_RUN` 只覆盖 4bpp，8bpp 全透明层仍逐像素走完全程（r2 541 ms/60f = **9.0 ms/帧**，0 个 opaque px）。
6. **口径修正（已改 `gba_device.cpp`）**：C 只累加真正进入像素循环的 run，打印却除以全部 runs，跳过率高的层被低估（r1 显示 164，真值 1141）；现按 `runs − skip` 归一，下一份日志以新口径为准。
7. 已清理：`fetch bench`（XIP 取指 8 cyc/iter 已定论）。

**N2 合并 devtick `rest` 的 4 次 `cycles_until_*`**（❌ 已取消，2026-11-21 `haltX` 定论）

- 原估 `rest` 309 ms/60f，但 `haltX` 实测泵内 next-event 扫描（`scan`）只占 **≈10 ms/60f（0.05% wall）**，`body` 也只 0.60 s（3.5%）。原 309 ms 里的大头是 `tick_devices` 在 HBlank 事件里触发的 render，不是 `cycles_until_*`。
- 收益上限 = 0.17 ms/帧，**不足以抵消一次烧录与回归成本 ⇒ 不做**，本项不再跟踪。

**N3 逐像素体治理**（按 N1 实测重写；原「4bpp nibble 批解码」判定为收益有限，降级并入 N3b）

- **N3a 8bpp 零 tile run 跳过**（低风险，独立，先做）：把 `GBARECOMP_PPU_BG_ZERO_RUN` 的「32 B 全零即跳过」扩展到 `color256`（8bpp 一个 run 读 32 B、可 8 次 32 位读或 2 次 `memcpy`；非 4bpp 的 tile 行同样与像素语义无关，位等价）。
  - 实现（2026-11-20，待烧录验证）：`gba_ppu.cpp` 的 ZERO_RUN 块新增 `color256` 分支，两次 32 位读 `tile_row..tile_row+7`，`(lo|hi)==0` 即 `PPU_BG_SKIP` + 跳过；越界（`tile_row+8 > 96K`）仍走旧路径。编译通过，`Binary size 16,372,260 B`（+112 B）。
  - 依据：r2 全透明 8bpp 层 541 ms/60f = 9.0 ms/帧，0 opaque px。
  - 预估：该层 `bgL` 541 → ≈150–200 ms/60f（run 成本从 451 降到 ≈100–160 cyc/run），省 **6–7 ms/帧**。
  - 验收：`bgL r2` 与 `bgRun r2` 的 skip 数同步上升；画面逐像素一致；`bg` 总量下降与 r2 降幅吻合（≤0.5 ms 误差）。
  - 首测（14:02 日志）：① 赛道段（14:38 后）4 层全是 4bpp（`bg2 4d07`），**没有 8bpp 层 ⇒ N3a 在此场景不生效**；② 14:22 的 `dispcnt 7f40` 场景 `bg2 = 858a`（8bpp、93% 不透明、带透明边框），r2 的 skipped 从「改动前的 0」变为 **133,500/288,000（46%）**，按 4bpp 全透明 run 的 347 cyc/run 折算省 ≈3 ms/帧；③ 同步对照 4bpp 层 r0/r1/r3 的 C 为 125–167 cyc/px，与改动前同量级 ⇒ 无回归。因 skip 计数同时包含窗口跨度跳过，8bpp 零 tile 的精确收益只能靠「赛道无 8bpp」这一事实间接确认，不再另加探针。
- **N3b 逐像素体瘦身**（低风险，位等价，N3a 之后单独烧）：① 在 run 循环前把 `vram` / `pal` / `g_row_top` / `g_row_second` / 窗口行指针提升为局部常量，消除闭包双重解引用；② 行级判定「本层整行窗口全开 / 整行混合全开」，命中时删掉逐像素 `layer_enabled()` / `blend_enabled()`；③ 透明索引判定前移，命中 `continue` 的路径最短化。
  - 实现（2026-11-20 已上机，开关 `GBARECOMP_PPU_BG_PIXEL_LEAN`）：① 在 `render_regular_bg` 内提升 `bg_vram` / `bg_pal` 两个局部 `const` 指针（lambda 按引用捕获外层，原来的每次使用都要先取闭包帧指针）；② `render_scanline_impl` 在 `row_control` 就绪后把整行 240 个字节 AND 成一个 `row_control_all`（仅 `any_window` 时执行，~0.3 ms/帧），`render_regular_bg` 据此得到 `layer_window_all` / `layer_blend_all`，逐像素 test 退化为常量比较；③ 8bpp 与 4bpp 都在调色板取数前先判 `pal_idx == 0` 直接 `continue`，4bpp 的 bank OR 也移到判定之后。`g_row_top` / `g_row_second` 未提升：批处理分支里只有关掉的 N3b-RUN_SKIP 会读它们。
  - 回退：`GBARECOMP_PPU_BG_PIXEL_LEAN=n` 即回到旧逐像素形式（`layer_window_all = layer_blend_all = no_window`），不动其他编译单元。
  - 预期分场景（实测未兑现，见首测③）：赛道段 `dispcnt 1f40` 的 bit13–15 全 0（无窗口），旧代码已靠 `no_window` 短路掉逐像素调用 ⇒ 该场景的增量只来自指针提升与零索引前移（估 10–20 cyc/px）；带窗口的 `7f40` 场景（WIN0/WIN1/OBJWIN 全开）本以为能吃到行级 mask，实际 `row_control_all` 恒不命中。
  - 编译：通过，`Binary size 16,372,156 B`（−104 B，对比 N3a 后 16,372,260 B）。
  - 首测（17:03 日志）：① **8bpp 层同状态对比成立**——`dispcnt 1e40`、`bg2 858a`，`bgOP r2 1,149,360/1,236,000` 与 `bgRun r2 288,000/133,500` 与 14:02 日志完全一致（同一画面位置），`bgSeg r2` 的 C **1446 → 1046 cyc/run（−27.7%）**，该层 `bgL` **1089 → 839 ms/60f（−23%）**，211.5 → 162.9 cyc/px。② 4bpp 满不透明层（r3，288,000 runs / 0 skip / 2,304,000 px 全覆盖全不透明）本次为 C=1239（`bgL` 1635 ms、170.3 cyc/px）；旧固件同场景值未留存（14:02 日志已删），只能定性判断无回归。③ **行级窗口 mask 在真实画面上没有命中**：`7f40`（WIN0+WIN1+OBJWIN 全开）本次跑到两块 60f（17:06:07 / 17:06:18），r3 的工作量与 ② 完全相同（288,000 runs / 0 skip / 2,304,000 px），C 反而升到 **1457 / 1623**（`bgL` 1898 / 2097 ms，197.7 / 218.4 cyc/px），8bpp r2 的 C 1046 → 1259 / 1414。机制：`row_control_all` 要求「该层在整行 240 px 都不被窗口裁掉」，真实画面总有部分列被裁 ⇒ `layer_window_all` 恒 false，逐像素 `layer_enabled()` 仍在，只多付 ≤240 次/行的 AND 汇总（≈0.3 ms/帧）。④ 赛道段本日志只有两块 60f：17:08:33 进场过渡（`1f40`、objscan 8），17:08:49 含起跑特效（`3f40` 仅开 WIN0、interp 761 ms、dma 97 ms），逐层 C 1219–1359；r0 覆盖率 851k vs 旧赛道块 435k，不可直接归因。
  - 结论：**N3b 通过、保留**，但收益来源与立项预期不同——**全部来自指针提升 + 零索引前移**（8bpp 层 C −27.7%）；行级 mask 是「命中才赚、不命中不亏」的兜底，**原定「带窗口场景 ≥25%」在真实 ROM 内容上不可达，目标作废**。新验收口径：任何场景逐层 C 不劣于旧基线、逐像素零差异，透明像素多的层有正收益。继续压 per-pixel 只能走 N3c（`submit` 内联 / 候选结构 12 B → 8 B），不再在窗口判定上做文章。
  - 依据：反汇编 ~60 条指令/像素、每像素重复读指针；全透明路径 43 cyc/px 说明骨架本身就有压缩空间。
  - 预估：带窗口场景 C 从 152 → 90–110 cyc/px（`bg` 省 6–9 ms/帧）；无窗口的赛道段只看指针/零索引两项，预计 −10~15 cyc/px。
  - 验收（17:03 首测后修正）：逐层 C 不劣于旧基线、逐像素比对零差异，8bpp/高透明层须有正收益。原「带窗口场景下降 ≥25%」不适用——行级 mask 在真实内容上不命中（见首测③）。
- **N3c `submit` 内联 / 候选结构瘦身**（条件项，仅当 N3b 后 C 仍 >100 cyc/px）：`submit` 走 `[[gnu::always_inline]]` + 快路径只做 `top[x]` 比较；12 B 候选若压到 8 B 需重排 `PixelCandidate`，风险高，最后再议。

**赛道段实测画像（14:02 日志，14:38 后稳态，驱动 N3/N5 的优先级）**

- `wall` 17.1 s/60f = **285 ms/帧（3.5 fps）**：`disp` 12.59 s（**74%**）| `halt` 4.29 s（25%）| `pres` 0.18 s。
- `render` 4.61 s = 77 ms/帧：`bg` 49.4（`bgL` 33.7 + affine 15.1）| `obj` 14.0 | `out` 9.1 | `prep` 2.7 | `win` 1.2。
- `bg` 逐层（cyc/px，C 段用新分母）：r0 125 / r1 151 / r2 144 / r3 167；C 合计 **26.6 ms/帧**（`bgL` 的 79%）。
- 三处与旧基线不同的结论：① 赛道段是 4 层 4bpp（`bg2 4d07`）+ affine BG2；② 最大项回到 AOT 派发（74%），render 只占 27%，`bg` 17%；③ `memtot` 30 ms/帧（490 cyc/call）、`devtick` 18 ms/帧，均高于 bg 单层成本。
- 口径：N3b 若把 C 砍 35%，赛道段约省 9 ms/帧（+3% fps）；要把赛道 fps 拉起来必须回到 `disp`（N5）。

**赛道段复测（17:03 日志，17:08:26 后，N3b 固件）**

- 17:08:33 进场过渡（`dispcnt 1f40`，无窗口，4 层 4bpp，r0 2,304,000 px 全不透明）：wall 7.70 s/60f，render 2890 ms（bg 1915，其中 r0 1659 ms = **173 cyc/px**），bgSeg r0 C=1270，objscan 仅 8。
- 17:08:49 正式赛道（`dispcnt 3f40`，仅 WIN0）：wall **15.38 s/60f（3.9 fps）** = `disp` 9997（65%）+ `halt` 5155（33%）+ `pres` 181。`render` 4504 = `bg` 3170（r0 706 / r1 450 / r2 311 / r3 784）+ affine `a2` 892 + `out` 533 + `prep` 173；`disp` − `flush` 的 4368 ms 旧口径误读为 AOT，实际含 `interp` 761（229 calls，65 kinsns）与未知量的 `disp_flush`（21 日 haltX 复测为 1.1 s）；`dma` 97；`memtot` 读 1009 + 写 442 = **1451 ms/60f（24 ms/帧、占总 wall 9%）**，为全日志最高。
- 两处新观察：① `disp calls` 3358/60f（≈56 次/帧）与 `interp` 761 ms 集中在含起跑特效的这一块，不能当稳态基线；② **`flush` 归属未定**——`render_scanline` 由 `tick_devices` 在 HBlank 事件里触发，落在哪个分支就记给哪个桶，所以 `halt` 5155 ms 里可能大半是 render，`disp` 9997 ms 里的 AOT 也未必是 ≈3.6 s。已加 `haltX` 探针（`halt` 内 flush 计时 + next-event 扫描计时 + 反推 `disp_flush`），见 N4。
- 结论：赛道段可确定的只有 `render` 29%（4504 ms）与 `memtot` 9%；`disp`/`halt` 的 98% 拆分待 `haltX` 一次烧录定论。`bg` 逐层 C 1219–1359 与旧赛道块不可比（r0 覆盖率 851k vs 435k），继续压 `bg` 只剩个位数 ms/帧。

**N4 过场 / 菜单帧劣化归因**（调查，先归因再决定是否动手）

- 11-20 日志后段（08:49 起）`dispatch miss` 累计升至 1872，`disp calls` 197 → 2010–3358/60f，`interp` 757 ms/60f，wall 14–17 s。
- 判据：同帧 `bgL` 里两层全屏 4bpp 各 ≈1,385 ms（≈144 cyc/px）说明是 **render 变重**；只有 `interp` 占比明显上升才是 Tier3 回退。两者处置完全不同。
- 17:03 日志新增两条待归因项：① **`flush` 落在 `halt` 还是 `disp` 未知**（`render_scanline` 在 `tick_devices` 的 HBlank 事件里触发），所以 `halt`/`disp` 不能直接读成「泵开销」/「AOT」。已加探针 `haltX 60f: flush / body / scan | disp_flush`（`src/gba_device.cpp`，每泵 3 次 DWT 读：next-event 扫描、`g_flush_cyc` 前后差、泵总时长；`disp_flush = flush − halt.flush`），固件 `16,372,468 B` **已上机并定论，走判据 A**（见下）。② **窗口开启本身的逐像素成本**：同工作量 4bpp 满不透明层 C 1239（`1e40`）→ 1457 / 1623（`7f40`），即 +27~48 cyc/px，属 N3c 可打目标。
- 判据（`haltX` 结果三分支）：A) `halt.flush` ≈ `render + devtick + dma` 且 `body` ≈ `scan` ≈ 几十 ms ⇒ 渲染是在 HALT 期间借道完成，赛道真实主体是 `disp` 里的 AOT ⇒ 立项 N5（寄存器提升）；B) `disp_flush` 数百 ms 以上、`body` 明显 >0 ⇒ `disp` 混入了渲染，先修账再谈优化；C) `body` 上千 ms 且与 `render` 无关 ⇒ 泵内确有大头（事件队列 / `runtime_tick` 记账），按桶继续细分。
- 产出：一条归因结论。若确为回退主导，先算补种子会让派发表 + 代码各涨多少（NOR 只剩 ≈235 KiB），再决定是否立项。

**haltX 首测结论（2026-11-21 10:06 日志，赛道在 10:09:39 后）**——已定论，走判据 A

- 探针自洽：`halt.flush + body + scan ≈ halt`；标题/菜单段 `body` 72–200 ms、`scan` 7–18 ms、`disp_flush` 59–556 ms ⇒ 该段 `halt` 基本就是 render 借道。
- 赛道稳态（10:11:17–10:12:24，`dispcnt 1f40` 无窗口）：wall 15.7–17.1 s/60f、`disp` 11.1–12.8 s、`halt` 4.2–4.4 s；`halt.flush` 3.6–3.8 s、`body` **0.60–0.61 s**、`scan` ≈10 ms、`disp_flush` 1.8–2.3 s。
- 取 10:12:24 块（wall 17145、render 4683、devtick 1023、dma 98、flush 5847、halt.flush 3557、disp_flush 2289、interp 67、memtot 1816）拆分：
  - `halt` 4161 = render ≈2794 + devtick ≈600 + dma ≈60 + **body 604** + scan 8；
  - `disp` 12772 = **AOT ≈10.4 s** + interp 67 + render ≈1889 + devtick ≈400；
  - ⇒ 赛道真实构成 **AOT 61%（≈174 ms/帧）/ render 27% / devtick 6% / pres 1%**；`memtot` 1.82 s = AOT 的 17.5%；逐层 `bgL`：r0 372/435k（205 cyc/px）、r1 411/423k（233）、r2 439/602k（175）、r3 847/1,065,600（191）、affine a2 919/1,238,400（178）。
- 三条结论：
  1. `halt` 的 33% 不是泵开销，而是 render 借道；`body` 只占 wall 3.5%、`scan` 0.05%，**N2（合并 `cycles_until_*`）收益上限 ≈1 ms/帧，取消**。17:03 日志的「232 µs/泵」是错误算法（把 render 当成泵本体），已废弃。
  2. 赛道主瓶颈是 **AOT 指令执行 ≈174 ms/帧（61%）**；`interp` 仅 67 ms、`dispatch miss` 累计 3,280 无影响 ⇒ 提 fps 必须动 AOT ⇒ **N5 转正**（不再是条件项）。
  3. 渲染侧只剩 27%，继续投入上限 ≈+1 fps；N3c 降为顺手项。4bpp 满不透明层 C 1351（10:12:24）与旧赛道块 1334 持平，符合「无窗口场景只吃指针提升」的预期。

**N5 Phase 2-E E1 寄存器提升**（✅ 已转正，2026-11-21；唯一能压 AOT ≈174 ms/帧的主项）

- 立项依据（`haltX` 判据 A）：赛道稳态 wall 15.7–17.1 s/60f 中 **AOT ≈10.4 s（61%）**、render 27%、devtick 6%、pres 1%；`interp` 67 ms、`dispatch miss` 累计 3,280 ⇒ 不是 Tier3 回退，是纯 AOT 每指令成本。
- 现状成本：每 guest 指令 300–400 宿主周期，构成是 `g_cpu.R[]` 全内存访存 + 派发二分查找（12 B/条、2 MB 常驻 NOR）+ 每指令 `runtime_tick` 调用链；codegen 无寄存器分配。
- **实测反证（2026-11-21，本轮做掉的成本区）**：
  1. `objdump -d --disassemble=gf_tfunc_080002CC` 显示 `g_cpu` 基址已由 GCC 提升到寄存器（`str r3,[r4,#60]` 一类），即「`uint32_t *const R = g_cpu.R;`」式指针提升收益 ≈0，**不做了**。
  2. 真正的每指令开销是 2–3 个 `BL`（`cpsr_c`、`arm_set_nzc_logic`、`runtime_tick`），约 54 B 代码/guest 指令；生成 `.text` 合计 12.68 MB、全 ROM 有 232,206 处 `runtime_tick(` 调用点。
  3. 尝试把 `cpsr_*` / `arm_set_nz*` 改成 `always_inline`（开关 `GBARECOMP_AOT_INLINE_FLAGS`，已回退）：链接直接报 `region 'ROM' overflowed by 1,577,852 bytes`。**结论：ROM 主分区只剩 240,392 B（≈235 KiB），任何 per-call-site 代码增长都不可接受**；凡是要在 232k 个调用点各加几字节的方案一律否决。
- 目标（修正）：只改**被调用函数内部**（调用点代码体积不增），在 300–400 cyc/指令 上砍 20–30%；赛道 AOT ≈174 ms/帧 ⇒ 目标 ≈120–140 ms/帧（+0.6–0.9 fps）。
- 拆步（按收益/风险排序，一次只烧一处，各自独立宏开关）：
  1. **`bus_read_*/bus_write_*` 入口短路**（低风险、收益最确定）：`memtot` 占 AOT 17.5%（1.82 s/60f，即 ≈30 ms/帧），砍入口的 IO 判定 / trace / resync 固定开销；不碰 codegen，不动 ROM 体积。
     **✅ 已实现（2026-11-21，开关 `GBARECOMP_BUS_FASTPATH`）**：搬进 [`runtime_bus_bridge.cpp`](../third_party/gbarecomp-main/src/runtime/runtime_bus_bridge.cpp) 的 6 个 `bus_read_u8/u16/u32`、`bus_write_u8/u16/u32` 入口。EWRAM/IWRAM/ROM（0x08–0x0D，含 4 MB 以上的镜像）直接走 `read_memory` / `write_memory` + epoch，跳过 `is_io_addr`/`is_idle_safe_read` 双判、`note_mmio_read`、`sync_bios_access`（2 次 store）、`apply_bus_read_override`、`trace_unmapped_read`（out-of-line 调用）以及写侧的 `runtime_resync_horizon`。位等价前提：①GPIO 窗口 0x0C0–0x0CB 留在慢路径；②BIOS（`addr < 0x4000`）留在慢路径（`bios_access_enabled_` 依赖 PC 锁存）；③`g_runtime_bus_read_override` 非空时整体回退慢路径；④写路径 epoch 自增顺序不变。ROM 读不消费 `open_bus_pc_`（`gba_bus.cpp` ROM 分支只用 `matrix_`/`rom_`/GPIO），故跳过 `note_pc` 安全。构建验证：`Binary size 16,372,988 B`（+520 B），ROM 余量 ≈235 KiB。预期收益上限 ≈ memtot 的份额，实测以 `memrd`/`memwr` 与赛道 fps 为准。
     **✅ 上机验证（11-21 14:37–14:39 赛道段，保留）**：`memtot` 1.82 → 1.16–1.63 s/60f（30.3 → 19–27 ms/帧），单次访存 ≈490 → **≈430 cyc（−13%）**，无 fault / 无画面异常。但赛道 wall 只从 15.7–17.1 降到 14.8–17.9 s/60f（前 5 块均值 15.3，**净收益 ≈+2–4% fps**），因为 memtot 只占 AOT 17.5%，且后段 guest 访存次数自然上涨（IWRAM 读 318k→538k、写 203k→330k）吃掉了入口节省。前 5 块 AOT 8.3 s、后段 10.6–10.9 s（与基线 10.4 s 持平）⇒ 收益确实封顶在入口固定开销，**AOT 主体必须靠第 2/3 步**。旁证：本次 Tier3 `dispatch miss` 累计 3,214 次（基线 51）、赛道 `interp` 21–759 ms（基线 67 ms），与总线桥无关，另立 N6 核实 `0x0805f6xx` 区域。
  2. **`runtime_tick` 内部瘦身**（低中风险）：现状每指令都做 `g_host_service_hook` 检查、`cyc_probe` 记账、`drain_dma_steal`、IRQ 判定；改成慢路径外置 / 预算窗口内批量，函数体积可持平。
     **✅ 已实现（2026-11-21，开关 `GBARECOMP_TICK_LEAN`）**：`runtime_tick` 每 guest 指令执行一次，旧快速路径 ≈51 条指令 + 3 个外调，本次只砍调用开销、不改语义：①去掉每指令 `cyc_probe("tick", cycles)`（环境变量门控的诊断，设备默认关闭）；②DMA steal 改为先内联读 `pending_dma_steal_cycles() != 0`，为 0 时不进入 `drain_dma_steal()`；③IRQ 判定改为 `GbaIo::irq_pending_inline()` 直读 IE/IF/IME（与 `irq_pending()` 同三位）。位等价依据：`drain_dma_steal()` 第一件事就是取 steal 计数、为 0 即返回，且单线程无副作用；内联 IRQ 判据与 `irq_pending()` 读取同一组 IO 寄存器。构建验证：`Binary size 16,372,980 B`（−8 B，ROM 余量 240,396 B），`[gba] runtime features` 含 `GBARECOMP_TICK_LEAN`；`objdump --disassemble=runtime_tick` 确认热路径已无 `bl cyc_probe` / `bl drain_dma_steal`（改为 `ldr`+`cbz`）/ `bl irq_pending`（改为 `ldrh` 直读）。预期收益 ≈ 每指令省 3 次 BL + 函数体（≈10–20 cyc/指令），实测以赛道 AOT 时间为准。
     **✅ 上机验证（11-21 15:09–15:14，三场景，保留）**：赛道（`3f40`，15:12:55–15:13:53 五块）wall **14,295–14,996 ms，均值 14,654（4.0–4.2 fps）**，对比 N5-1 前 5 块均值 15.3 s ⇒ **−4.2% wall（≈+4% fps）**；`disp` 8,670–9,558（均值 9,220，63%）、`halt` 5,049–5,417（35.6%，其中 render 4,221–4,549 ≈30%）、`pres` 181 ms；`interp` 154–732 ms（0.15–0.73 s，3.7%）。`memtot` read 643–683 + write 658–714 = 1.30–1.40 s/60f，单次 read 317–324 cyc、write 664–699 cyc，与 N5-1 持平（入口短路无回退）。标题（15:09:45–15:10:08）6.3–9.3 s/60f（6.5–9.6 fps，disp 1.8–5.3 s / render 2.5–3.5 s）；标题菜单（15:10:21–15:11:04，2–4 层 BG）6.8–10.5 s/60f（5.7–8.8 fps）；选择菜单（15:11:15–15:12:18，`1741`，3D OBJ）11.3–16.6 s/60f（3.6–5.3 fps），**render 5.4–7.8 s 占 49%**、`disp` 47% ⇒ 该场景已由渲染主导，tick/AOT 瘦身上限 ≈1%，要提 B 段必须动 OBJ/affine 渲染。三场景均不低于 §1 的 A/B/C 基线（5.3–5.5 / 3.5–4.5 / 3.6–4.0 fps）。注意：N5-1 的日志已被覆盖，A/B 两段无法做同版本严格对拍，只能与基线表比较；N5-2 单独归因以赛道段为准。旁证：Tier3 `dispatch miss` 累计 2,196（15:13:44）仍在增长，新增 `0x08052fxx` / `0x08050bxx` 热点，N6 仍待核实。**下一步候选（暂未启动，待定优先级）**：①OBJ/affine 渲染批量化（B 段 render 占 49%，是当前 AOT 之外最大单块）；②N6 核实 `0x0805f6xx` / `0x08052fxx` / `0x08050bxx` Tier3 热点（赛道 `interp` 0.15–0.73 s/60f）；③N5-3 codegen 去冗余 `R[15]` 存储（需重生成 ROM 并对拍，ROM 余量 ≈240 KB）。
  3. **codegen 去掉冗余 `R[15]` 存储**（中风险，能换 ROM 余量）：每 guest 指令前后各写一次 `g_cpu.R[15]`，可由 codegen 只在分支/异常边界写；需重新生成 ROM 并对拍，**先减体积再谈其它 codegen 改动**。
  4. 若 1–3 仍不足，才考虑寄存器分配式 codegen（需先腾出至少 1 MB ROM 余量，或同步做 E2 派发表压缩）。
- 回退：每一步独立宏开关（`GBARECOMP_*`），codegen 改动保留旧生成路径（`--no-regalloc`）以便对拍。

**N6 低优先待补**

- 补验按键 B / Select / L / R（11-17 日志未覆盖），以及 KEY1/KEY2 → GBA 按键的备份映射。
- 三条固定验收路径（标题 / 选择菜单 / 赛道）在明确按键操作下重取对照日志；无按键只有标题画面。

**明确不在范围**：音频输出、存档（依据「性能不达标，音频和存档没有意义」）。按下高亮需要「面板级 fb + 局部 dirty rect」，暂不做。

**风险**

- `third_party/gbarecomp-main` 不受 git 管理（被 `.gitignore` 忽略），其改动只存在于工作区；动 `gba_ppu.cpp` 等文件前必须整体快照，否则无法精确回退。
- `patches/0001-gbarecomp-sf32-hsp-adaptations.patch` 为累计补丁（针对 `third_party/gbarecomp.tar.gz` 的 `src/` 子树），**11-21 已随 N5-2 重新导出并 `git apply --reverse --check` / 净版正向 `--check` 双向全绿**（覆盖 10 个文件，新增 `gba_io.h`；bridge 227 行 / ppu 622 行，74,578 B）。改 third_party 后须按 `tar 解压 → git diff --no-index --no-prefix a/… b/…` 重导并跑一次反向校验，否则补丁会再次失真。
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
| 2026-11-21 | N5-1 入口短路：`memtot` 单次 490→430 cyc，赛道 +2–4% fps；N5-2 `runtime_tick` 去三处外调：赛道均值 15.3→14.65 s/60f（4.0–4.2 fps），标题/菜单无回归；两项均保留 |
