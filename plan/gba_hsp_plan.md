# GBA ROM 重编译到思澈黄山派 实施计划

> 最后更新：2026-11-19

## 0. 当前状态总览

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 0 | 环境 + LCD 点亮（SDK example/lcd_fb 验证） | ✅ 完成 |
| Phase 1 | PC 端 gbarecomp 重编译 ROM/BIOS → C++20 产物 | ✅ 完成（Mario Kart SC 4MB；8 shard；slim 后 12.68MB `.text`） |
| Phase 2-A | 仓库根独立 scons 工程骨架 | ✅ 编译通过（`main.bin` 325,240 B） |
| Phase 2-B | 显示后端：中央 240×160 RGB565 | ✅ 完成（当时的「滚动彩条」实为 165×15 坐标 bug，已在 2-C 修复） |
| Phase 2-C | runtime 移植到 SiFli HAL | ✅ 完成并真机验证（BIOS 从 reset vector 跑到空闲态，236 个函数入口，无 fault；修复送屏几何后居中 240×160 白块，2026-11-11 复烧确认） |
| Phase 2-D | 真实 ROM 接入：TF 卡读入 PSRAM + cart dispatch 联动 | ✅ 完成并真机验证（2026-11-13 12:12）：屏幕已出 Mario Kart 游戏画面；入口种子 `0x08000000` 生效；9 个 miss PC（`0805F624` 等）由 Tier3 解释器自愈，累计 51 次后收敛，全程无 FATAL；`frame 720 / vblank 741` 持续推进，≈5.0 fps（8.4% 实时）。路线：数据区函数指针表补 413 种子会 ROM 溢出 2,474,764 B ⇒ 改 Tier3（≈9.6 KB）。见 §0.1–§0.3。性能优化中（2026-11-16：≈7.2 fps / 13.9% 实时）；2026-11-17 的 BG run 优化因画面异常整批回退，见 §0.4–§0.6；目标已由 60 fps 修正为 7→10 fps，见 §0.7–§0.8；11-17 逐步小改三组实测见 §0.9（6.7–7.3 fps 赛道 / 9.0 菜单）；11-17 16:29 加上虚拟手柄后，赛道 3.3–5.9 fps，见 §0.10；11-18 加入跳帧（隔帧渲染）并真机验收，render 3.46→1.7–2.1 s/60f，7.2→8.7–10.0 fps，见 §0.11（验收后已关闭开关，回到每帧基线以优化 disp）；11-18 访存探针实测：访存只占 `disp` 12–16%，瓶颈在 AOT 指令执行与 render，候选方案 A/B/C 已归档，见 §0.12–§0.14；当前执行 B（恢复 render 探针），每帧基线 7.7–8.3 fps；B 分段已出数（bg 占 render 56–77%、obj 其次）、B-1 八桶已实测（全段停留主界面：重 bg 路径 bg 76%、OBJ 路径两桶 49%），见 §0.15；B-2 已细分为 B-2a–d 并定回退纪律，B-2a（bg 按层细分探针）已烧录实测：标题/菜单/赛道三段分层数据出齐，bg 由 text 层主导，下一步 B-2c；赛道瓶颈已转为 `disp`（73%），见 §0.16；11-19 缓存/XIP 体检结案：I/D cache 与 MPU 全开、XIP 取指净 12 cyc ⇒ 缓存不是瓶颈，新增 `flush` 子桶探针（dma/recfg/devtick）待烧录，见 §0.16 |
| Phase 2-E | 容量优化（E1 寄存器提升 / E2 派发表压缩 / E3 模板表去重） | 📋 候选已归档（见 §3.2.4）。2-D 未超限（ROM 余 353 KB），非阻塞；Tier3 已使补种子路线不再必要，是否实施待定 |
| Phase 3 | 音频 / 按键 / 存档 | 🔄 进行中：**按键输入已跑通**（3.1 step 1–3 完成，2026-11-17 16:29 真机验证 Start/A/十字键均能驱动游戏）；音频与存档经决策暂不做，性能优化暂缓，见 §0.10 |

**烧录已打通**：`sftool 0.2.5`（`C:\Users\zhang\.sifli\tools\sftool\0.2.5`）+ `build_.../uart_download.bat`，串口 COM14（CH340），三镜像 `ftab@0x12000000` / `bootloader@0x12010000` / `main@0x12020000` 写入成功，屏幕 **(75,96)-(314,255)** 窗口出画面（11-17 起窗口上移，上下留给虚拟手柄）。

> **约定：后续下载烧录由用户本人执行**，AI 只负责编译并给出烧录命令。

**TF 卡与 ROM 加载**（2026-11-11 首烧验证）：SD 卡识别为 29.29 GB SDHC（SPI1 / `spi_msd` 注册 `sd0`），
`dfs_mount("sd0","/","elm",0,0)` 成功；ROM 固定放卡根 `/gba.gba`，开机整份读入 PSRAM（`bus bind` 924 ms）。

> **Phase 2-D 已完成**（2026-11-13 12:12 烧录，屏幕出游戏画面），证据见 §0.3。
> 后续不再为「让派发表覆盖所有函数」而补种子，miss 一律由 Tier3 解释器兜底。

> **下一步（优先级待用户定）**：① 补验 B / Select / L / R（2026-11-17 日志未覆盖）；
> ② 按键按下高亮（需「面板级 fb + 局部 dirty rect」）；③ KEY1/KEY2 → Start/Select 备份映射；
> ④ 性能：访存探针已给出归因（访存仅占 `disp` 12–16%，见 §0.12–§0.13），
> 按 §0.14 执行 **B**（恢复 render 分段探针、继续压 `halt`），之后依次 A（AOT 指令执行）、C（访存/IO 快路径）；
> ⑤ 性能 B 完成后回到 A，音频与存档不在范围。

### 0.1 Phase 2-D 问题与决策（均已于 2026-11-13 关闭）

**HALT 死锁**：`halted=1` 后 `runtime_should_yield()` 返回 true 是正确行为（把控制权交还宿主），
但设备 run loop 未移植 host 的 `pump_idle()` ⇒ 无指令执行 ⇒ 无 `runtime_tick()` ⇒ `cycles` 不前进
⇒ 到不了 scanline 160 ⇒ 无 VBlank IRQ ⇒ `clear_halt()` 永不执行，永久闭环。
修复落在宿主循环：halted 时按 `min(ppu/io/audio 下一事件)` 推进 `runtime_tick(chunk)`。
同轮把出帧改为宿主每轮 `present_frame()`（slim codegen 已删光卡带侧 `yield`，present hook 不可达），
按 `g_runtime_vblank_starts` 去重，一个 VBlank 只刷一次。
另：`bus bind` 的 924 ms 是 `GbaBus::set_rom()` 全盘扫描 4 MiB 的代价，非故障。

**`yield` 粒度结论**（当时无真机证据即已确定，现仍有效）：

1. `yield` 混着两类：**采样/检查类**（栈 guard、断点、trace、watchdog、PC 采样）与**契约类**（present、halt 交还）。
   SLIM 删掉 per-instruction 调用本身合理——设备无调度器可切，采样类本来 inert。
2. 真正的失误是没把「卡带无 yield 点 ⇒ 出帧/HALT 推进/栈水位由宿主循环负责」写成显式契约，
   使 `pump_idle`/present 变成对 host runner 位置的巧合依赖。因此改为宿主循环负责并在上方维护契约清单。
3. 不采用「保留 yield 只删采样类」：成本主体是不可内联的每指令调用（留空壳照付），
   契约类需求只有 60 Hz 级，相差约 20 万倍，两头都不占。应按「钩子需要的触发频率」分层。
4. SLIM 下 `yield` 无法级联展开（只在函数入口命中，返回后调用者继续执行），
   栈保护若确实需要，必须是 codegen 新增回边/基本块级插入点，属于新机制。
5. **重新决策的触发条件**：真机出现深调用链卡死或栈溢出，或出现必须在中途（非 dispatch 边界）触发的钩子。

### 0.2 入口 miss 与数据表 miss → Tier3 自愈（2026-11-13）

**入口 miss `0x08000000`**：BIOS 用 `mov r12,#0x08000000` 交棒，而生成器默认 `--entry 0x080000C0`，
ROM 导言（`2E 00 00 EA`，一条 ARM `B`）被当作 trampoline 跳过，派发表里没有 `0x08000000`，
设备端 miss 处理随即 FATAL 硬停。
按生成配置修正，不动 codegen、不在宿主循环里特判：`roms/mario_kart_sc.toml` 增加
`[[extra_func]] addr=0x08000000 / mode="arm" / name="cart_rom_entry"`，重新生成后派发表首项为
`0x08000000 → gf_cart_rom_entry`（函数体即一条 `B`，尾调 `gf_start_vector`），`main.bin` 仅 +92 B。

**第二类 miss：数据区函数指针表**。`0x0805F624` 全 ROM 只有 `0x080F325C` 一处引用，属
`0x080F3230–0x080F325C` 的 13 项函数指针表；同表 9 个目标已编译，4 个没有。数据区指针不是控制流，
静态发现不扫表 ⇒ 这类盲区**无法靠逐个补种子系统性消除**：

| 全 ROM 扫描口径 | 数量 |
|------|------|
| 值落在 ROM 范围内的 32 位指针 | 9,264 |
| 连续 ≥3 指针的表 / 不同目标 / 未入派发表 | 561 段 / 3,356 / 1,967 |
| 目标在 64 半字内到 `bx lr` / `pop {pc}`（像真函数） | 413 |

把 413 个写进 seeds 实测：派发表 167,911 → 189,504（+21,593 项 ≈259 KB）、函数 19,805 → 22,905、
shard 8 → 16、代码 +≈2.6 MB，LINK 直接 `region 'ROM' overflowed by 2474764 bytes`（当时余量 380,459 B）。
原因是每个漏项都是调用图根，会递归带入整片子系统。`roms/mario_kart_sc.seeds.toml` 仅作证据，不参与生成。

**改为运行时降级解释执行（上游 Stage 1 self-heal，Tier3）**：解释器四文件
（`interpreter` / `thumb_decode` / `arm_decode` / `arm_ir`）实测仅 ≈9.6 KB `.text`，与 2.6 MB 差三个数量级。

1. `src/SConscript` 编入 `arm_cpu_bridge.cpp` 与解释器四个源文件；删掉 `src/gba_device_stubs.cpp` 里的
   `mul_wait_cycles` 存根（改由 `arm_ir.cpp` 提供，避免重复定义）。
2. `src/gba_device_stubs.cpp`：`runtime_dispatch_miss` 不再 `halt`，记录 miss PC（每个只打印一次）后调用
   `runtime_bridge_interpret`；bridge 复刻宿主契约：stop 地址收敛、SWI 交 `runtime_swi`、逐指令 `runtime_tick`、
   共享尾声回溯裁栈、单次上限 5,000 万指令（跑飞仍 FATAL，不静默挂死）。
3. 保持不变：`overlay_try_dispatch` 恒 0（设备无编译器）；`runtime_unimplemented_op` 仍硬停（那是 codegen 缺口，
   不是发现盲区）。

编译（2026-11-13，清理临时探针后）：`main.bin` 16,352,268 B，ROM 余 371,910 B，
仅剩既有的 `g_force_interp` 告警与 RWX 段告警，无未定义符号、无重复定义。

### 0.3 Phase 2-D 真机验证（2026-11-13 12:12，COM14）— 屏幕已出游戏画面

`窗口1_COM14_(2026-11-13-12-12-17)_ui.log` 关键证据：`cart: "MARIO KART" AMKE, 4194304 B`、
`bus bind 924 ms`、`cart entered: pc=08000000`（入口种子生效）、
`dispatch miss pc=0805f624 -> interpreter (total 1)` 无 FATAL、`frame 720 / vblank 741`。

- **Tier3 有效性**：9 个 distinct miss PC（全在 `0x0805F6xx–0x0805F7xx`，与 §0.2 定位的 `0x080F3230`
  表目标区一致），累计 51 次后 `total` 不再增长 ⇒ 每个漏项只在首次进入时解释，收敛回静态入口后不再重复；
  全程无 `FATAL bridge`（无解释器缺口、无跑飞）。
- **运行状态**：`frame 60→720`、`vblank 60→741` 单调递增；`pc` 在卡带代码、BIOS HALT 续跑点 `00000348`
  与 IWRAM `03002424`（游戏在 RAM 中执行/自拷贝）之间往返；当时的状态探针显示 `irqd=1`（`mode=12`）
  VBlank IRQ 链正常、`hal` 0/1 交替 HALT 泵持续工作。
- **性能**：148.1 s 墙钟推进 208.98 M guest 周期 ⇒ ≈1.41 MHz、**8.4% 实时 / 5.0 fps**，
  与「仅 BIOS」阶段（5.1 fps）持平 ⇒ Tier3 不在热路径上，无可测回退。
- **调试痕迹清理**（2026-11-13）：删除 1 Hz `dbg` 状态定时器与 `cart_ent/disp/idle` 计数、启动期堆/PSRAM
  打印、每入口 `bios enter` trace、未读的 bridge 指令计数；保留 `cart entered` 一次性里程碑、
  `bus bind` 计时与每 60 帧 `frame` 心跳。
- **结论**：Phase 2-D 关闭。不再补种子、不改 codegen；下一步候选由用户定：Phase 3（音频/按键/存档）
  或先做性能（当前 ≈5 fps，目标实时）。

### 0.4 性能测量（2026-11-13，进行中）

**14:05:59 日志的帧率结论**（`窗口1_COM14_(2026-11-13-14-05-59)_ui.log`）：`frame 60`（14:06:16.703）→
`frame 960`（14:09:31.771）＝ 900 帧 / 195.1 s ≈ **4.6 fps**；`vblank 60→981` ≈ 4.72 Hz ≈ **8% 实时**。
帧速率随场景变复杂从 ~5.0 fps（240→360 段）缓降到 ~3.9 fps（840→960 段）。
GBA 帧预算 280,896 guest 周期 ≈ 16.74 ms，实测 ≈217 ms/帧 ⇒ 240 MHz 下 ≈**185 主机时钟 / guest 周期**。
已排除：Tier3 不在热路径（miss 停在 51）；读卡只占开机 `bus bind` 924 ms。

**耗时归属假设**（待探针数据裁决，按证据强度）：

1. `dispatch_resolve` 每次块间跳转对 167,911 项派发表（≈2.5 MB，NOR XIP）二分 ≈18 次随机读，基本全 cache miss；
   前面还有 `lookup_native_ram`（256 项 hash cache）、trace / force-interp 检查。
2. 12.68 MB `.text` + 2.5 MB 派发表全部 XIP，依赖 cache。
3. codegen 无寄存器分配：每条 guest 指令都是 `g_cpu.R[]` 内存读写（Phase 2-E 的 E1）。
4. PPU 全软件渲染 + 每帧 38,400 px RGB888→RGB565 + 76.8 KB LCD 同步 flush，与 guest 串行、不重叠。
5. 单线程串行；`pc` 常在 BIOS `00000348`（HALT/IntrWait 续跑点），HALT 泵粒度也可能参与。

**已加的临时探针**（未提交，全部带 `Temporary` 注释，随时可删）：

- `src/gba_device.cpp`：DWT `CYCCNT`（`0xE0001004`，`perf_start()` 在 `reset_cpu()` 后使能 TRCENA/CYCCNTENA）
  累计三桶，每 60 帧打印
  `[gba] perf 60f: wall N ms (X cyc) | disp N ms N calls | halt N ms N pumps | pres N ms N`。
  `wall` 同时给 ms 与 DWT 周期，用于核对 240 MHz 假设（应 ≈240,000 cyc/ms）。
- `src/main.c`：`gba_lcd_present()` 内拆分转换与推送，每 60 帧打印
  `[gba] lcd 60f: convert N ms, flush N ms`。
- 编译结果：`main.bin` 16,352,908 B（+640 B），ROM 余 371,130 B，仅既有 `g_force_interp` / RWX 告警。

**判读口径**：

| 现象 | 结论 | 下一步 |
|------|------|--------|
| `disp ms / calls` 单次 ≫1 µs | 派发解析主导 | 在 `dispatch_resolve` 内部再切分（hash cache / 二分 / `lookup_native_ram`） |
| `halt pumps` 达数万/帧且 `halt ms` 占比高 | 空闲泵 + 每扫描线 PPU 渲染主导 | 加大泵粒度、渲染批量化 |
| `pres` 占比高 | 呈现链路主导 | 看 `lcd` 行 convert vs flush，再决定转换 SIMD 化 / 双缓冲 |
| `wall cyc / wall ms` ≠ 24 万 | 主频假设错 | 修正 `to_ms()` 的 `kCoreHz` 后重测 |

**构建命令（PowerShell）**：必须照 `set_env.bat gcc` 组装 PATH，缺 `tools\bin` 会在生成
`link_copy.lds` 时报 `The system cannot find the file specified`：

```powershell
$env:SIFLI_SDK='E:/work/gitee/sifli/SiFli-SDK/v2.5.1'; $env:SIFLI_SDK_PATH=$env:SIFLI_SDK
$env:RTT_CC='gcc'; $env:RTT_EXEC_PATH='D:\sifli\env\tools\gnu_gcc\arm_gcc_14.2.1\bin'
$env:PYTHONHOME='D:\sifli\env\tools\python-3.11.9-amd64'
$env:PYTHONPATH="D:\sifli\env\tools\python-3.11.9-amd64;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\build;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\build\default"
$env:PATH="D:\sifli\env\tools\git\Git\bin;D:\sifli\env\tools\bin;$env:RTT_EXEC_PATH;$env:PYTHONHOME;$env:PYTHONHOME\Scripts;D:\sifli\env\tools\qemu\qemu32;D:\sifli\env\tools\cmake\bin;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\menuconfig\dist;E:\work\gitee\sifli\SiFli-SDK\v2.5.1\tools\scripts;" + $env:PATH
scons --board=sf32lb52-lchspi-ulp -j8
```

坑：直接调 `scons` 可能命中 Python 2.7 的 `scons.bat`（`site.py` SyntaxError），`Scripts\scons.exe`
全路径调会 `SRE module mismatch`；按上面的 PATH 注入后用 `scons` 即可，或 `python.exe -m SCons`。
本次构建因根目录 `.sconsign.dblite` 已被移入回收站而触发全量重编译（**禁止访问/恢复回收站**，重建即可）。

**等待中**：用户烧录后取 1–2 分钟（进赛道）日志，按上表定位后再决定改哪一层；探针数据出来前不动优化代码。
工作区状态：`src/gba_device.cpp` / `src/main.c` 含探针、本文件含本节，三者**均未提交**（最近提交 `0c60681`）。

### 0.5 性能：-Oz 根因与 `gba_ppu.cpp` 单文件 -O2（2026-11-16）

**基线（B 组，`...14-38-43_ui.log`，关片上显存）**：帧 540–780（MK 菜单）`wall 9.67–10.24 s/60f`、
`render 4602–4712 ms`（`bg 3229–3340`、`out ~895`、`prep 312`、`obj 108`）；
帧 ≥1020 换场景后 `bg 6242–6250`、`render 8325 ms`、`wall 14.1 s/60f`。

**根因**：`build_.../compile_commands.json` 显示所有 runtime 源文件都吃 SDK 全局 `-Oz`。
-Oz 会关掉 GCC 几乎所有内联启发式，而 `render_scanline_impl` 的内层全是 lambda / helper
（`submit`、`layer_enabled`、`blend_enabled`、`load_u16_le`）；`prep` 与 `out` 两阶段的开销形态
正是「调用没内联 + 每像素走内存」的典型特征。

**改动**：`src/SConscript` 把 `gba_ppu.cpp` 从 `gba_runtime` 的 glob 结果剔除，单独
`DefineGroup('gba_ppu', ..., LOCAL_CCFLAGS=' -O2')`。`building.py` 的 `local_group()` 把
`LOCAL_CCFLAGS` 拼在全局 `CCFLAGS` 之后，GCC 后者覆盖前者 ⇒ 实际命令为 `-Oz ... -O2`，
只有这一个文件走 -O2。只放一个文件是因为 `0xFD8000` 分区余量有限（`main.bin` 16,367,452 B，
+4,892 B；链接表 ROM 余 356,222 B）。

**A/B 结果（`...15-10-14_ui.log`，帧 540–780 同场景）**：

| 阶段 | -Oz | -O2 | 变化 |
|------|-----|-----|------|
| `bg` | 3229–3340 ms | 2567–2741 ms | -18% |
| `out` | ~895 ms | 499–509 ms | -43% |
| `prep` | ~312 ms | 184–186 ms | -41% |
| `obj` | ~108 ms | 93–94 ms | -13% |
| `render` | 4602–4712 ms | 3400–3575 ms | -25% |
| `wall` | 9.67–10.24 s | 7.99–8.57 s | -16% |

重场景（帧 ≥1020）：`bg` 6242→5144、`render` 8325→6567、`wall` 14.1→11.9 s。

**现状归属（139 ms/帧）**：`disp` 57（guest AOT 执行，不在 -O2 覆盖范围）｜`halt` 78
（其中 `render` 58 = `bg` 44 + `out` 8.4 + `prep` 3.1 + `obj` 1.6；`audio` 6；`ppu` tick 3.9；`vbl` 3.9）｜
`pres` 3。

**`bg` 剩余 44 ms 的判读**：每层每行实测 ≈17,131 cyc。代码结构上 4bpp 每个 8 像素 run 有 1 次
map 读 + 4 次 tile 字节读（`cached_addr` 只在 run 内复用），再加每像素片上调色板读与 `submit`；
按 `mem bench` 的 PSRAM 70.5 cyc/次 × ≈150 次/层行 ≈ 10,500 cyc，与实测同量级 ⇒ **bg 大半耗在
VRAM 的 PSRAM 往返**上。

**待验证疑点**：`GBARECOMP_ONCHIP_DISPLAY_MEM=y` 那组与关闭组的 `bg`/`obj` 完全一致
（3207–3336 vs 3229–3340；109 vs 108）。96 KB 数组整体换到片上不可能零变化，怀疑那次构建的宏
没进 `CPPDEFINES`（唯一证据是构建日志里的 `[gba] runtime features:` 行，已丢失）。若宏真的生效，
按上条估算 `bg` 应约为一半（~1400 ms/60f）。

**下一步（按顺序）**：
1. 打开 `CONFIG_GBARECOMP_ONCHIP_DISPLAY_MEM`，构建后**先核对** `[gba] runtime features:` 含该宏，
   再烧录对比 `bg`。RAM 侧余 244,772 B，96 KB VRAM + 2 KB 放得下。
2. 4bpp tile 行改 32 位读：`tile_row = char_base + tile*32 + py*4` 天然 4 字节对齐，一次
   `load_u32_le` 出 8 像素，每层行可省 ≈90 次 PSRAM 读（不依赖片上显存）。
   → **2026-11-17 已尝试并整批回退（画面异常），见 §0.6；重做前必须先做离线逐像素等价性验证。**
3. 以上都不动时，再考虑 `disp`（guest 执行，57 ms/帧）侧：`runtime_bus_bridge.cpp` 等放开 -O2
   的体积/收益比不如前两条。

### 0.6 BG run 优化整批回退（2026-11-17）— 画面异常，回到正常基线

**经过**：按 §0.5「下一步」第 2 条实现 4bpp run 优化（日志 `...08-24-31_ui.log`），真机画面出现大面积
块状马赛克 + 蓝底。

1. **nibble 解码 bug（已修）**：快路径把 run 首 nibble 用 `tile_bits << (28 - 4*begin)` 移进高 4 位后
   每像素 `nib <<= 4`，非镜像 run 只有第一个像素有效（其余全 0），`begin > 0` 时还会反序。改为循环左移
   `rot = hflip ? 4*begin : 28 - 4*begin`、`nib = (tile_bits << rot) | (tile_bits >> ((32 - rot) & 31))` 后
   色块消失，`subm/px` 由 43% 升到 98%（`...09-15-37_ui.log` frame 360：`px 1497508 subm 1470123`）。
2. **`need_second` 门控（确认不安全，已回退）**：半透明 OBJ（OBJ mode 1）的 `target1` 不依赖 BLDCNT
   1st-target 位，门控为 `first_targets != 0` 时 `second[]` 会残留上一行数据，输出阶段错误混合。
3. **剩余异常未定位**：nibble 修好后用户仍反馈「左侧图形、白云明显异常」。逐项复核快路径与通用循环的
   调色板索引（`(index|bank)*2`）、透明判定、target 位、HUD 字段、`x += count` 均判为等价，代码层无解
   ⇒ 按用户指示整批回退到画面正常的版本。

**回退基线来源**：`third_party/` 被 `.gitignore` 忽略（`git ls-files` 无记录、`git diff` 为空），git 无历史可回退；
改用编辑器本地历史快照 `%APPDATA%\Trae CN\User\History\-2d8eb064\fkck.cpp`（2026-11-16 17:12，perf7 之前）。
`git diff --no-index` 显示两版差异**全部**集中在 BG run 循环 + 探针，因此只回退这一段：

| 已删除（perf7） | 已恢复（基线） |
|------|------|
| 4bpp/no-window 快路径分支（nibble 轮转、`bgfast` 计数） | 通用逐像素循环（`layer_enabled` / `blend_enabled` 逐像素判定） |
| 4bpp tile 行 32 位预取（`tile_bits` / `tile_bits_ok` / `load_u32_le`） | `cached_addr` / `cached_byte` 逐字节缓存（每 run 4 次 tile 读） |
| 全零 run 整段跳过 | 逐像素透明判定 |
| `need_second` 门控 | 无条件维护 `second[]` |

~~保留 `PPU_PHASE_STAMP` 与 `PPU_BG_*` 探针、`perf4`/`bgfast` 打印~~ —— 这些探针在
2026-11-18 随 §0.12 的访存探针一起替换：`PPU_PHASE_*`/`PPU_BG_*`、`perf2`–`perf4`、
`bgfast`/`bgpix`/`bgzero` 已全部删除（其中 `PPU_BG_PIX_STAMP` 是每像素两次 DWT 读，
删除本身也降低了渲染耗时）。

**回退后构建**（`build_perf9.log`，`-j4`）：`main.bin` 16,367,788 B，ROM 用 16,257,599 B（余 **355,777 B**），
RAM 余 278,460 B，仅既有 `g_force_interp` / RWX 告警。**真机复烧确认画面恢复正常**（用户 2026-11-17 反馈）。

**代价**：perf7 的 BG 加速全部作废，`bg` 回到 §0.5 的 ≈44 ms/帧水平；`main.bin` 比 perf7 小 ≈0.8 KB。

**后续约束（避免重演）**：
1. `third_party/gbarecomp-main` 无版本控制 ⇒ 动 `gba_ppu.cpp` 前先把该文件整体快照出去（或重新导出
   `patches/0001-*`），保证任何一步都能精确回退。
2. 每个 BG/OBJ 渲染优化必须先在 PC 端做**逐像素等价性验证**（同一帧分别用通用循环与优化循环渲染，
   比对 `latched_fb_`），通过后再上机。
3. 一次只改一处、单独烧录验证，避免再出现「整批可疑、只能整批回退」。
4. 已知线索（未解决，重做时可先查）：日志中每帧有 2304000 px（= 240×160×60，精确一整层）走通用循环且
   0 次 `submit`，说明存在一个全透明层（>`tile_bits_ok` 范围或 256 色），可作为纯性能目标先做整层跳过。

### 0.7 60 fps 不可行：真机实测归因与目标修正（2026-11-17）

数据源 `窗口1_COM14_(2026-11-17-10-44-44)_ui.log`（回退后的正常基线，稳定段 60 余个统计窗口）。

| 层 | 实测/帧 | ÷ 16.74 ms 预算 | 说明 |
|------|------|------|------|
| `disp` guest AOT 执行 | 60.9 ms（197 calls） | **3.6×** | 光跑 CPU 就已超预算 |
| `render` 软件 PPU | 121.2 ms | **7.2×** | bg 99.7 / out 7.4 / obj 7.0 / prep 6.2 / fgn 0.1 |
| halt 内其他 | ~17.9 ms | 1.1× | audio 6.4 / vbl 3.9 / ppu tick 3.8 / tim 2.6 / qry 0.9 |
| `pres` 送屏 | 3.2 ms | 0.2× | lcd convert 2.7 + flush 0.4，非瓶颈 |
| 合计 `wall` | 205.8 ms（4.86 fps） | 12.3× | |

**三条实测结论**：

1. **天花板 ≈11.8 fps**：render 完全归零也只剩 84.6 ms/帧。60 fps 在本平台不可达，与优化幅度无关。
2. **bg 是计算瓶颈，不是访存瓶颈**：153,600 采样 px/帧、156 host cyc/px，其中 142 cyc/px 在逐像素解码循环，
   tile fetch 仅 0.34 cyc/px（2.7 ms/60f）。片上显存已生效（SRAM 14.0 vs PSRAM 70.6 cyc/read），
   §0.5 曾怀疑的「bg 耗在 PSRAM 往返」被否决。
3. **近一半解码被丢弃**：`subm 4,250,949 / px 9,216,000 = 46%`，窗口裁剪 / 遮挡 / 全透明层的工作白做。

**与立项预测的偏差**：§3.2.2 按 14.3 host cyc/guest cyc 估 4.0M cyc/帧 = 满速 60 fps；实测 49.4M cyc/帧
（176 cyc/guest cyc），**超预算 12.3×**。仅 `disp` 一项也比预算高 3.6×（52 cyc/guest cyc）。

**立项评估漏掉的三点（写入后续评估口径）**：

1. **把 ASIC 算力当成免费**。GBA 的 PPU / DMA / 音频 / 定时器是专用硬件，与 CPU 并行且不占 CPU 周期。
   真机 PPU ≈5 guest cyc/px（并行、零 CPU 开销），软件模拟 156 host cyc/px（串行、独占 CPU）。
   正确预算是各模块**相加**，不是取 max。
2. **对 cache miss 零建模**。派发表 2.5 MB + `.text` 12.68 MB 全 XIP；`dispatch_resolve` 每次块间跳转
   二分 ≈18 次随机读，基本全 miss。14.3 cyc/guest cyc 的模型只有「指令数 × cycle/指令」。
3. **codegen 无寄存器分配**。每条 guest 指令都是 `g_cpu.R[]` 内存往返（Phase 2-E1 未做），
   这是 `disp` 超预算 3.6× 的直接成因。

**今后的硬上限判据**（任何「实时」目标先过这三条，任一不过即否决）：

1. **每像素预算**：16.74 ms × 240 MHz ÷ 38,400 px = **104 host cyc/px**，须覆盖全部 BG 层 + OBJ + 混合 + 输出。
   当前单 BG 层 156 cyc/px ⇒ 当场否决。
2. **等效主频**：纯软件扮演 16.78 MHz ARM7 + 全套 ASIC，经验需 ≥1 GHz 级通用核；240 MHz M33 差约一个数量级。
3. **串行惩罚**：真机并行的模块（PPU / DMA / 音频）必须单独核算独占时间，不得并入 CPU 预算。

**目标修正**：60 fps → 分阶段 **7 → 10 fps**（当时赛道 4.86 / 菜单 7.2；2026-11-17 16:29 复测：开场/菜单 6.2–7.5、赛道 3.3–5.9）。60 fps 不再作为验收项。

### 0.8 EPIC 2D 加速能否用于 PPU（2026-11-17，评估结论：只做边角）

**硬件在位**：52x 有自研 2D 引擎 EPIC（ePicasso），`CONFIG_BSP_USING_EPIC=y`，LCD 驱动已在
`drv_lcd_fb_init` 里调用 `drv_gpu_open`（日志 `drv.epic: drv_gpu opened`），HAL 见
`drivers/Include/bf0_hal_epic.h`。能力：图层 blend / fill / 渐变 / 缩放旋转 / A2-A8 mask /
RGB565-ARGB8888 格式转换 / EZIP 解码；52x 限 **2 普通层 + 1 mask 层**。

**关键限制（决定它救不了主瓶颈）**：

1. **不支持调色板索引格式**（`EPIC_COLOR_*` 全是直接色 + alpha/gray/mono，无 CLUT）。
   GBA 的 BG tile 是 4bpp/8bpp 调色板索引，必须先展开成 RGB 才能喂给 EPIC —— 而展开正是
   142 cyc/px 的主成本。即「EPIC 省不掉解码」。
2. **没有 tilemap 模式**。EPIC 的图层是线性矩形位图（width/height/stride），GBA BG 是 8×8 tile 表 +
   tile 数据 + 每行 SCX/SCY 卷动，两者不匹配。
3. **层数不足**：52x 只有 2 普通层 + 1 mask，而 GBA 一帧最多 4 BG + 2 OBJ + 窗口 + 5 种混合效果，
   只能多趟合成，每趟都是整帧 PSRAM 往返（70 cyc/read）。

**能拿到的收益（有界）**：

| 可迁移项 | 现耗时 | 说明 |
|------|------|------|
| 输出阶段格式转换 + 送屏 | out 7.4 + lcd convert 2.7 ms | EPIC 直接输出 RGB565 到 LCD FB，省掉 CPU 转换 |
| OBJ 精灵（缩放/旋转/mask 一次性 blit） | obj 7.0 ms | OAM 仿射精灵正好匹配 EPIC transform + mask |
| Affine BG（Mode 1/2/7 旋转缩放） | 含在 bg 内 | 需先有线性源位图，仅对少部分场景有效 |
| BLDCNT 层间混合（α 1/2 / 加亮 / 变暗） | 含在 bg 内 | EPIC 是硬混合，但要求输入已是分层的线性位图 |

合计乐观估计 **省 10–15 ms/帧**（205.8 → 约 190 ms，4.86 → ≈5.2 fps）；且 EPIC 是 DMA 异步执行，
可与 CPU 重叠，能再回收一部分。**无法触动 99.7 ms 的 bg 解码主体**，因此不改变「60 fps 不可行」的结论。

**并行路线（已排除）**：52x 的 LCPU 是蓝牙专用协处理器，算力与内存带宽均不适合承担渲染，
不列入候选（2026-11-17 决策）。因此 EPIC 只做 out / obj 的边角加速，主线仍是单核内的软优化，
目标按 §0.7 的 7→10 fps 执行。

### 0.9 逐步小改的三组真机实验（2026-11-17，每步单独烧录验证）

按「一次只改一处 → 编译 → 用户烧录 → 回传日志且确认画面正常」的节奏推进，三组结果如下。
每组都是独立 `GBARECOMP_*` 宏，可单键回退；`MARIO KART` 4 MiB ROM，240 MHz。

| 时间 | 宏 | 结论 | 处置 |
|------|-----|------|------|
| 14:00:58 | `PPU_BG_ZERO_RUN` | **有效** | 保留（default y） |
| 14:36:52 | `PPU_BG_RUN_SKIP` | 无效 | 代码保留，`default n` |
| 14:57:36 | `PPU_RESTRICT_BUFFERS` | 无效 | 整块删除 |

**1. `PPU_BG_ZERO_RUN`（跳过全透明 4bpp tile run）——有效。**
把 8 像素 4bpp run 的四个 tile 字节用一次 32 位读取出，四字节全零即整 run 跳过。
`render` 4.8–5.3 s → **3.46 s**（60 帧），`bg` 946–976 M → **626–628 M** cyc，逐像素 `dec` 352–380 M → **122 M** cyc；
菜单场景 **6.2 → 7.2 fps**。全透明层正对应 §0.6 线索 4 的「一整层 0 次 submit」。

**2. `PPU_BG_RUN_SKIP`（整 run 已被更近层全覆盖则跳过）——本场景无效。**
MARIO KART 不透明像素仅 **37.6%**（2.60 M / 6.91 M），8 像素 run 被更近层全覆盖的概率 ≈0.376⁸≈**0.04%**，
实测所有统计块 `bgskip 0 0`，该守卫从未触发。而它的前置是每扫描线扫 128 条 OAM（判断是否有半透明 OBJ），
是纯开销。**处置**：代码留在 `#if` 后，Kconfig `default n`，负结果写进 help；重场景是否另开由后续数据决定。

**3. `PPU_RESTRICT_BUFFERS`（候选行 `top[]`/`second[]` 加 `__restrict`）——零收益。**
与 14:36 版**逐项一致**：同负载稳态块（rows 28800 / runs 872480 / px 6912000 / subm 2601930）下
`bg 641.75 M / fetch 33.87 M / pix 525.90 M`、`dec 141.06 M / sub 248.88 M`（旧值 641.52 / 33.77 / 141.11 / 248.74 M）。
说明 GCC 并未被 char 别名屏障挡住，瓶颈不在别名。**处置**：宏、三处指针限定与白名单全部删除。

**当前帧率**（13:44/14:57 两个正常日志，按每 60 帧 `wall` 折算）：
单 BG 层菜单 **9.0 fps**（6657 ms）；正常赛道 **6.7–7.3 fps**（8270–8928 ms）；
重场景（rows 38400）**4.8–4.9 fps**（12170–12540 ms）。

**当前帧预算**（正常赛道块，137.8 ms/帧）：

| 项 | ms/帧 | 说明 |
|------|------|------|
| `render` | 58.8 | bg 44.5 / out 8.5 / obj 1.5 / prep 3.0 |
| `disp` guest AOT | 54.1 | 197 calls/60f，codegen 无寄存器分配（Phase 2-E1） |
| halt 内其他 | ≈17 | audio 6.0 / vbl 3.9 / ppu tick 3.8 / tim 2.5 / qry 0.8 |
| `pres` 送屏 | 3.2 | 非瓶颈 |

全串行相加即 `wall`。BG 内部 93 cyc/px（硬上限 104）；**最大单项是 `sub` 96 cyc/次提交**——
每次提交都写 12 字节 `second[]`，而 `effect != 1` 时该行无人读。下一步候选按此排序：
A) 免写 `second[]`；B) out 阶段批量化；C) `disp`（Phase 2-E1 寄存器分配）。
（2026-11-17 用户决策：这三项**暂缓**，先做按键输入；「性能不达标，音频和存档没有意义」→ 后续又定为音频/存档不做。）

### 0.10 Phase 3.1 按键输入：虚拟手柄 + 触摸映射（2026-11-17，真机可用）

数据源 `窗口1_COM14_(2026-11-17-16-29-09)_ui.log`。按「一次一小步 + 用户烧录确认」分三步：

| 步 | 内容 | 结果 |
|----|------|------|
| 1 | 触摸 / 实体键 bring-up（`src/gba_input.c`） | ✅ FT6146 报点方向正确（驱动内已翻转为面板坐标），KEY1/KEY2 可读 |
| 2 | 虚拟手柄绘制（`src/gba_pad.c`，纯 rect/circle/capsule 填充，无字体/图片/分配） | ✅ 用户确认布局与配色 |
| 3 | 触摸命中 → guest `KEYINPUT` | ✅ 游戏可操作（Start 进菜单、A 确认、十字键移动） |

**屏幕几何**：GBA 窗口由居中 `(75,145)` 上移到 **`(75,96)-(314,255)`**，上方 96 行只放 L/R，
下方 194 行放十字键与 AB/Select/Start。机身铺满整屏（GBA 紫 `RGB565(74,62,132)`），
灰按键 `RGB565(196,196,204)`，边框 `RGB565(104,100,136)`。

**布局与命中**（`band_y0 = 281`，即窗口底 255 + 1 + `BAND_TOP_GAP 25`）：

| 控件 | 位置（面板坐标） | 命中形状 |
|------|------|------|
| 十字键 | 中心 (72, 353) | 方框 ±61，中心 ±19 死区；越出死区按轴向判，可同时给两个方向 |
| A / B | (330, 334) / (262, 384) | 圆 r=30 |
| Select / Start | (178, 329) / (178, 377) | 圆 r=16，竖排在十字键右侧（参考 GBA 面板） |
| L / R | (18,28)-(95,65) / (294,28)-(371,65) | 胶囊 |

命中判定不另存常量，直接复用 `gba_pad_draw()` 记下的 `g_pad_band_y0` 与
`controls_ready` / `shoulder_ready` 标志，绘制与触摸区不会漂移。十字键用方形框而非精确十字轮廓，
是为了在电容屏上更容易搓出对角（四角未绘制区域也会触发方向，属有意设计）。

**映射路径**：`gba_pad_hit()` → guest 位序的按下掩码（bit0 A … bit9 L）→ `gba_input.c` 取反并掩
`0x03FF` → `gba_device.cpp: gba_device_set_keyinput()` → `g_bus->io().set_keyinput()` → `IoReg::KEYINPUT`。
UP 清 0，DOWN/MOVE 重新命中，掩码不变则不写寄存器。日志 `[gba] pressed=0x%03x keyinput=0x%03x`。

**日志证据**：

| 报点 | 结果 | 判定 |
|------|------|------|
| (330,353) / (322,355) / (321,358) | `pressed=0x001` → `keyinput=0x3fe` | A ✅ |
| (175,389) / (169,387) | `pressed=0x008` → `0x3f7` | Start ✅ |
| (72,316) | `pressed=0x040` → `0x3bf` | Up ✅ |
| (20,360) | `pressed=0x020` → `0x3df` | Left ✅ |
| (304,355) / (311,365) / (325,365) | 无 `pressed` 行 | A/B 之间的空隙，正常 miss |
| (153,379) / (1,348) / (1,369) | 无 `pressed` 行 | 十字键方框外，正常 miss |

**两个实测结论**：

1. **滑动不发 MOVE**：按住滑动时驱动连续上报的是 `ev=2`（DOWN）而非 `ev=3`（MOVE）——
   16:33:11:873–12:078 连续 16 条 `ev=2`，坐标从 (324,328) 平滑走到 (319,348)，全程没有 `ev=3`
   （整份日志 0 条）。当前 DOWN/MOVE 走同一分支，所以不影响手感，但「MOVE 路径可用」这份日志证明不了。
   按时序看，`tp ev=2` → `pressed=` 相隔约 3–10 ms，即一个 10 ms 轮询周期内完成映射。
2. **未覆盖**：B、Select、L、R 本次没按（无 `0x002/0x004/0x100/0x200`）；KEY1/KEY2 仍只打印电平，
   未映射到 GBA 按键；按下高亮未做。

**性能影响**：手柄只在开机画一次（与清屏共用那一次整屏 flush），之后每帧仍只刷 240×160 窗口
（`lcd 60f: convert 166 ms, flush 24 ms`）；输入线程 10 ms 轮询、只在掩码变化时写寄存器。
本份日志的帧率：开场/菜单 **6.2–7.5 fps**（`wall` 7942–9618 ms），进比赛后 **4.4–5.9 fps**，
重场景（`disp` 2000+ calls / `render` 8.5 s per 60f / 212k cyc per line）**3.3–3.5 fps**（`wall` 17086–18430 ms）。
与 §0.9 的 6.7–7.3 属不同场景，不是 GUI 回退。

### 0.11 跳帧（隔帧渲染）：两版实现与真机结论（2026-11-18）

数据源：`窗口1_COM14_(2026-11-18-11-00-07)_ui.log`（第一版，无效且有闪屏）、
`窗口1_COM14_(2026-11-18-11-29-36)_ui.log`（第二版，验收通过）。

**最终机制**（`CONFIG_GBARECOMP_FRAME_SKIP`，`kRenderEvery = 2`）：

| 位置 | 改动 |
|------|------|
| `GbaPpu::set_frame_skip(n)`（gba_ppu.h） | 设 `frame_skip_`，清零 `frame_phase_`；`frame_rasterized()` = `rasterization_enabled_ && frame_phase_ == 0` |
| `GbaPpu::tick()`（gba_ppu.cpp） | 帧结束（`scanline_` 回绕到 0）时 `if (++frame_phase_ >= frame_skip_) frame_phase_ = 0` |
| `render_scanline()` | 开头 `if (!frame_rasterized()) return;`——被跳过的帧只推进 dot/scanline、IRQ、DMA 与仿射累加器，不做合成 |
| `mark_framebuffer_latched()` | 仅 `frame_rasterized()` 时把 work 拷到 latched；跳过帧保留上一张完整画面 |
| `set_rasterization_enabled()` | **不再清零** work/latched 缓冲（清零是闪屏的直接原因） |
| `present_frame()`（src/gba_device.cpp） | 每个 VBlank 都送屏（跳过帧重复上一张）；perf 改按 `vbl % 60` 上报，`pres` 如实反映送屏次数 |

**第一版为什么无效**（保留为教训）：用 vblank 回调按 `vbl` 奇偶切 `set_rasterization_enabled()`。
游戏里该回调并非每帧触发（`pres 30/60`），vbl 奇偶恒定 ⇒ 从未真正关闭 ⇒ `rows 19200`（60 帧全画）、
`render` 3.34 s 与基线 3.46 s 持平。同时 `set_rasterization_enabled(false)` 会清零缓冲，
跳过帧锁存黑帧，导致开机动画到游戏之间黑/白交替闪。
结论：跳帧判定必须由 PPU 按「帧」自己计，不能依赖宿主呈现回调的节奏，也不能清缓冲。

**第二版真机数据**（MARIO KART，240 MHz，每 60 guest 帧一行 `perf`）：

| 时间 | wall | disp | halt | pres | render | rows |
|------|------|------|------|------|--------|------|
| 11:30:31 | 5991 ms | 2609 | 3169 | 60 | 1973 ms | 9600 |
| 11:30:38 | 6448 ms | 3393 | 2843 | 60 | 1674 ms | 9600 |
| 11:30:50 | 5978 ms | 2818 | 2948 | 60 | 1755 ms | 14400 |
| 11:30:57 | 6512 ms | 3372 | 2928 | 60 | 1751 ms | 14400 |
| 11:31:04 | 6884 ms | 3400 | 3271 | 60 | 2074 ms | 14400 |

| 判据 | 无跳帧 | skip=2 | 说明 |
|------|--------|--------|------|
| `rows` | 19200（60×160×2） | 9600 / 14400 | 14400 = **30 帧** × 160 行 × 3 层；若不跳帧需 60 帧 × 1.5 层，稳态 BGCNT 层数只能取整数 ⇒ 只能是半帧光栅化 |
| `subm/px` | — | 69% / 37.7% | 分别对应 2 层 / 3 层（底层不透明），与 `rows` 推断一致 |
| `render` | 3.46 s | 1.67–2.07 s | −45%，与 skip=2 预期一致 |
| `pres` | 30/60（第一版） | 60/60 | 每个 VBlank 都送屏，画面不丢、不闪 |
| `wall` | 8385–8427 ms | 5978–6884 ms | **7.2 → 8.7–10.0 fps**（+21%~+39%） |

过渡段（GAMEBOY logo → 蓝天白云）`rows 0 / 960` 属正常：白屏渐变时 BG 全关，
该段耗时几乎全在 `obj`（786–1074 ms），即 logo 的 OBJ 绘制。

**帧预算变化**：`render` 58.8 → **29 ms/帧**；`disp` 54.1 ms/帧（≈2.6–3.4 s/60f）成为第一项。
继续加大跳帧（`kRenderEvery=3`）收益递减（最多再省 ~0.6 s/60f），因此下一步转向 `disp`。

**处置（2026-11-18）**：宏保留（Kconfig `default n`）。验收后已在 `proj.conf` 注释掉
`CONFIG_GBARECOMP_FRAME_SKIP`，回到「每帧渲染」基线并在此基线上优化 `disp`；
需要复测时取消注释、重新编译即可。

### 0.12 disp 拆解：访存探针（2026-11-18）

跳帧关闭后，每 60 帧预算约为 `wall 6.0–6.9 s = disp 2.6–3.4 s + halt 2.8–3.3 s + pres 0.19 s`，
`disp`（`runtime_dispatch` 调用 + 内部执行）是最大单项。`disp` 单次调用均值 ≈ 43–57 ms/帧，
换算成 guest 周期是 ~37–48 host 周期/guest 周期，等价于 **2.8–3.9 MHz 的 guest 吞吐**。
`mem bench` 显示 PSRAM 随机读 71 cyc、片上 SRAM 14 cyc，且 GbaBus 的 ~386 KB 模拟内存全在 PSRAM，
因此要先确认 `disp` 里到底有多少是访存、访的是哪个区域，再决定改哪一层——这一步不猜。

**探针实现**（`GBARECOMP_PERF_PROBES` 宏控，随该开关一起编译）：

| 位置 | 改动 |
|------|------|
| `runtime_bus_bridge.cpp` | `g_mem_prof_cnt[2][9]` / `g_mem_prof_cyc[2][9]`（[0] 读、[1] 写 × 9 区域）；`mem_prof_region()` 用地址高字节按出现频率排序比较；`MEM_PROF_BEGIN/END` 包住 `bus_read_u8/u16/u32`、`bus_write_u8/u16/u32` 六个入口，两次 DWT 读计时 |
| `src/gba_device.cpp` `perf_report()` | 新增 `memrd/memrd2/memwr/memwr2/memtot` 五行，打印各区域「访问次数/host ms」并清零 |

区域编号：0 BIOS、1 EWRAM、2 IWRAM、3 IO、4 PAL、5 VRAM、6 OAM、7 ROM(0x08–0x0D)、8 other（存档/开总线/未映射）。

**怎么读**：

- 探针开销约 10 cyc/次访问，按 60 帧约 2 M 次访问估算占 `disp` 的 ~2%，但 **桶内时间包含这部分开销**，
  故只看区域之间的相对关系，以及 `memtot` 的分摊平均值，不要把桶和直接与 `disp` 相减。
- `memtot ... (N cyc)` 是实测的单次访存总代价（含入口的 `is_io_addr`/`note_mmio_read`/`sync_bios_access`/
  override/trace）。与 `mem bench` 的 14 / 71 cyc 对比：若显著更高，说明入口固定开销（而非真实读写）占比大，
  可以走内联/裁剪路线；若接近 71，则是 PSRAM 往返主导，应考虑把热点区域搬到片上。
- 判定表：

| 现象 | 结论 | 下一步候选 |
|------|------|------------|
| EWRAM/IWRAM 的 ms 占 `disp` 大头 | guest 数据流量主导 | 开上游 `GBARECOMP_NATIVE_MEMORY`（`native_ram_read/write` 直通，绕过 bus 全路径） |
| IO 占大头 | 每帧轮询 MMIO（按键/STAT/DISPSTAT） | MMIO 读缓存 + 轮询快路径 |
| 各区域合计远小于 `disp` | 时间不在访存 | 转向 AOT 指令本身与 `runtime_tick`/IRQ 入口，量 Tier3 解释器占比 |
| PAL/VRAM/OAM 占大头 | PPU 逐像素访存 | 注意 §0.9：片上显存已实测无收益（0.3–1.2%，且画面异常），不要再走 |

**状态（2026-11-18）**：探针已编译进 `main.bin`（16,371,204 B，ROM 余 351 KB）；
真机数据见 §0.13，结论是**访存不是瓶颈（占 `disp` 12–16%）**，不上 `GBARECOMP_NATIVE_MEMORY`。

### 0.13 disp 归因实测：访存只占 12–16%（2026-11-18 15:46 日志）

日志 `窗口1_COM14_(2026-11-18-15-46-23)_ui.log`（跳帧关闭、每帧渲染）。区域行格式为
`访问次数 / host ms`，`memtot` 括号内是单次访问的平均 host 周期。

| 时段（60 帧） | wall | disp | halt | pres | 读 mem | 写 mem | mem/disp |
|---|---|---|---|---|---|---|---|
| 15:46:23 开机/BIOS 动画 | 9477 | 6000 | 3282 | 188 | 598 | 190 | 13.1% |
| 15:46:50 换场 | 10030 | 6025 | 3795 | 189 | 484 | 138 | 10.3% |
| 15:47:20 重负载场景 | 8068 | 6973 | 1395 | 156 | 621 | 207 | 11.9% |
| 15:47:35 / 43 / 50 赛道 | 7804/7724/7236 | 3751/3605/3065 | 3839/3907/3957 | 191 | 329/331/298 | 188/184/185 | 13.8/14.3/15.8% |

单次访存均值 `memtot`：读 284–457 cyc、写 159–504 cyc；而 `mem bench` 的 PSRAM 随机读只要 71 cyc。
说明入口本身（`is_io_addr` / `note_mmio_read` / `sync_bios_access` / override / trace / `resync_horizon`）
比真实搬运贵 3–6 倍。

区域拆解（赛道段 15:47:50 与开机段 15:46:23，括号为 cyc/次）：

| 区域 | 赛道 读 | 赛道 写 | 开机 读 | 开机 写 |
|---|---|---|---|---|
| EWRAM | 36,597 / 44 ms (289) | 37,094 / 25 ms (162) | 0 | 65,536 / 54 ms |
| IWRAM | 58,089 / 79 ms (326) | 48,615 / 44 ms (217) | 179,452 / 212 ms (283) | 135,441 / 86 ms (152) |
| ROM | 64,329 / 155 ms (578) | 0 | 155 / 0 | 0 |
| IO | 1,266 / 18 ms (3,400) | 2,661 / 115 ms (10,370) | 67,292 / 259 ms (924) | 854 / 16 ms (4,500) |
| BIOS | 77 / 0 | 0 | 94,703 / 119 ms (301) | 0 |
| PAL/VRAM/OAM | 0 | 0 | 4,864 / 6 ms、280 / 0、1,374 / 1、2,145 / 2 | — |

**结论**

1. **访存不是瓶颈**：赛道段读+写只占 `disp` 的 12–16%，占 `wall` 约 7%。
   即使开上游 `GBARECOMP_NATIVE_MEMORY`，能省的也只有 EWRAM+IWRAM 那 ~6%（192/3065 ms），
   且入口固定开销仍在 ⇒ **不做**，避免又一次「改了没收益」。
2. **`disp` 的主体是 AOT 指令本身**：扣掉访存后约 2.58 s/60 帧 ⇒ **≈37 host cyc/guest cyc**，
   与「codegen 无寄存器分配、每条 guest 指令都走 `g_cpu.R[]` 内存读写」的形态吻合（Phase 2-E E1）。
   这是目前最大的单项杠杆，但工作量也最大。
3. **IO 写异常贵**：2,661 次写花 115 ms（10,370 cyc/次），几乎全是 `bus_write_*` 末尾
   `if (io) runtime_resync_horizon()` 的调度同步代价。赛道段占 `disp` 3.8%，可做快路径但收益有限。
4. **`halt` 已与 `disp` 同级**（赛道 3.8–4.4 s vs 2.8–3.8 s），跳帧关闭后渲染回到每帧成本。
   注意：本轮清理把 `render/bg/obj/rows` 那几行一并删了，现在 `halt` 内部已无法再拆分；
   若要继续压渲染，需要先恢复这一行探针。

**下一步候选**（按收益/成本排序，待用户选）：

| 方向 | 预期 | 成本 |
|---|---|---|
| AOT 派发/指令执行优化（寄存器提升，E1） | 高（`disp` 主体的 37 cyc/guest cyc） | 高，改 codegen + 重新生成 ROM |
| 恢复 render 探针并继续压渲染（`halt`） | 中（`halt` 已占 50%+） | 低（一行打印），渲染侧已有 §0.5–0.9 积累 |
| IO 写 resync 快路径 + 访存入口内联 | 低（3–6%） | 低 |

### 0.14 待选优化方案 A / B / C（2026-11-18 归档）

**A. AOT 指令执行优化（Phase 2-E 的 E1）**

- 依据：§0.13 结论 2 —— `disp` 扣掉访存后 ≈37 host cyc / guest cyc，时间在生成的指令本身。
- 做法：codegen 对高频 guest 寄存器（r0–r7）做宿主寄存器提升并跨基本块保持映射，
  消除每条 guest 指令对 `g_cpu.R[]` 的内存往返；必要时叠加派发点内联。
- 预期：高（`disp` 是最大单项，理论可降 40–60%）。
- 成本/风险：高 —— 改 PC 端 codegen、重新生成 ROM、全量真机回归（标志位/内存序/自修改代码），体积可能变化。
- 状态：未开始，排在 B 之后。

**B. 渲染（`halt`）继续优化 —— 当前执行**

- 依据：§0.13 结论 4 —— `halt` 3.8–4.4 s/60f 已超过 `disp` 2.8–3.8 s，跳帧关闭后渲染回到每帧成本。
- 前置：恢复渲染分段探针（prep / bg / fgn / obj / out），上一轮「删没用 debug」把它一并删了。
- 做法：按分段读数定位，再沿用 §0.5–§0.9 的既有手段（`gba_ppu.cpp` 单文件 `-O2`、
  BG run 批量化、行级窗口缓存、热点搬片上 SRAM、`-Oz` 内联抑制的规避）。
- 预期：中；渲染侧已有完整历史基线和验证方法。
- 成本：低（一行打印 + 后续小步改动）。
- 状态：**进行中**（2026-11-18 起，探针已恢复并出数，见 §0.15）。

**C. 访存 / IO 快路径（收尾项）**

- 依据：§0.13 结论 1、3 —— 入口平均 446 cyc（读）/ 504 cyc（写），而 PSRAM 实测 71 cyc；
  IO 写 10,370 cyc/次，几乎全在 `runtime_resync_horizon()`。
- 做法：① `bus_read_*` / `bus_write_*` 对 EWRAM / IWRAM / ROM 做内联快路径，
  跳过 override / trace / resync；② IO 写在非 trigger 寄存器或值未变时跳过 resync。
- 预期：低（3–6%），因为访存总量只占 `disp` 的 12–16%。
- 成本：低，作为 A/B 之后的收尾。
- 明确**不做** `GBARECOMP_NATIVE_MEMORY`：它只能触及 EWRAM+IWRAM 的 ~6%，且入口开销仍在。

### 0.15 渲染分段实测：bg / obj 主导（2026-11-18 16:44 日志）

数据源 `窗口1_COM14_(2026-11-18-16-44-03)_ui.log`（跳帧关闭、每帧渲染）。`render 60f` 的五个桶是
同一扫描线的连续切片，**相加即整条扫描线的渲染耗时**；正常时段 `rows 9600` = 60 帧 × 160 行，
16:44:03/36/44 三个窗口只有 4,640–8,480 行（这些 60 帧里没有画满 160 行），比较时以 `cyc/row` 为准。
`render` 是 `halt` 内的一部分，`halt` 另含 HALT 泵与设备推进。

| 时段（60 帧） | wall | disp | halt | render | rows | cyc/row | prep | bg | fgn | obj | out |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 16:44:03 开机/logo | 9567 | 6058 | 3314 | 2420 | 8480 | 68,494 | 161 | 3 | 8 | 1798 | 448 |
| 16:44:20 菜单 | 6721 | 2155 | 4357 | 3256 | 9600 | 81,408 | 854 | 391 | 6 | 1563 | 440 |
| 16:44:52 赛道（轻） | 7508 | 2924 | 4372 | 3005 | 9600 | 75,140 | 183 | 2068 | 13 | 92 | 647 |
| 16:45:12 赛道（BG 重） | 10982 | 4014 | 6751 | 5416 | 9600 | 135,410 | 370 | 4095 | 15 | 424 | 510 |
| 16:46:10 赛道（精灵多） | 12670 | 4811 | 7642 | 6397 | 9600 | 159,937 | 188 | 4000 | 17 | 1586 | 604 |
| 16:47:23 重载赛道 | 16478 | 8479 | 7790 | 7250 | 9600 | 181,256 | 203 | 4084 | 19 | 2335 | 607 |
| 16:49:19 高 disp 段 | 15667 | 10397 | 5046 | 4067 | 9600 | 101,675 | 331 | 2715 | 17 | 468 | 533 |

渲染内部占比（占 `render`）：

| 时段 | prep | bg | obj | out | 帧率 |
|---|---|---|---|---|---|
| 16:44:03 | 6.7% | 0.1% | **74.3%** | 18.5% | 6.3 fps |
| 16:44:20 | **26.2%** | 12.0% | **48.0%** | 13.5% | 8.9 fps |
| 16:44:52 | 6.1% | **68.8%** | 3.1% | 21.5% | 8.0 fps |
| 16:45:12 | 6.8% | **75.6%** | 7.8% | 9.4% | 5.5 fps |
| 16:46:10 | 2.9% | **62.5%** | 24.8% | 9.4% | 4.7 fps |
| 16:47:23 | 2.8% | **56.3%** | 32.2% | 8.4% | 3.6 fps |
| 16:49:19 | 8.1% | **66.8%** | 11.5% | 13.1% | 3.8 fps |

**结论**

1. **`render` 占 `halt` 的 69–93%**（典型 75–85%），渲染仍是 `halt` 的主体；差额 0.5–1.4 s/60f
   是 HALT 泵本体（30k–52k 次/60f，折合 17–30 µs/次），属第二小项，暂不动。
2. **赛道段 `bg` 是第一大单项**：2.07–4.10 s/60f（34–68 ms/帧），占 `render` 56–77%，
   且换场景后稳定贴着 ≈4.1 s 的上限（`bg 4084/4095/4031/4108`）⇒ 层数/卷动决定成本，不是随机抖动。
3. **`obj` 随精灵数线性上涨**：92 ms → 2.34 s/60f（1.5–39 ms/帧），重载段占 `render` 32%；
   菜单/logo 段 BG 几乎停摆时 `obj` 反而 1.5–1.8 s（48–74%）⇒ 该桶里除 OBJ 精灵还包含 native draw jobs，
   两者必须拆开才能决定改哪个（见下）。
4. **`out` 与场景无关**：稳定 0.39–0.78 s/60f（6.5–13 ms/帧，占 8–22%），是 O(px) 固定成本
   （RGB888 组装 + 写行），轻载时占比被放大，是低风险可摘的一块。
5. **`prep` 有两个形态**：正常段 0.18–0.42 s（3–8%），但菜单段（16:44:20/28）**854 ms 占 26%**，
   同期 `bg` 只有 0.39 s ⇒ `prep` 里有与 BG 无关的固定扫描（窗口/OAM/图层状态），
   BG 停摆时才暴露；这一项此前从未单独归因。
6. **`fgn` ≈ 0**（2–20 ms/60f），foreign presentation 不是问题。
7. **每像素成本**：轻载 313 cyc/px、常规 427–565、重载 755 cyc/px；§0.7 的硬上限是 104 cyc/px。
   即使按 PSRAM 随机读 71 cyc/次折算，也相当于每像素 4–10 次随机取数 ⇒ 瓶颈在
   「逐像素解码 + 每层反复取 tile/调色板 + 逐像素提交」，不是单次访存延迟。

**B 的下一步候选**（一次只改一处，按风险/收益排序）：

| 序 | 动作 | 依据 | 风险 / 预期 |
|---|---|---|---|
| B-1 | 只加探针：`prep` 拆成「OBJ window 掩码 / 行列窗口解析 / 其余准备」，`obj` 拆成「候选扫描（OAM 解码+剔除）/ 像素填充」 | 结论 3、5：两个桶的构成未知 | 零风险；**已实现并编译通过**（`main.bin` 16,370,260 B），待烧录 |
| B-2 | 重做 4bpp tile 行 32 位预取 + 全零 run 跳过 | 结论 2：`bg` 占 56–77% | 中：§0.6 曾整批回退，**必须先做 PC 端逐像素等价性验证**（§0.6 约束 1、2） |
| B-3 | `out` 双像素打包（一次写 2 px，或直接产 RGB565） | 结论 4：0.39–0.78 s，场景无关 | 低；预期省 0.2–0.4 s/60f（wall 的 2–5%） |
| B-4 | HALT 泵粒度/空转裁剪 | 结论 1：0.5–1.4 s/60f | 低–中；需确认泵内除 render 外的时间去向 |

建议顺序 **B-1 → B-2 → B-3/B-4**：B-1 用最小代价把最后两个黑盒拆开，避免又在 `bg` 上重演 §0.6 的整批回退。

**B-1 实现记录（2026-11-18）**：设备走 `render_scanline_impl<false>`（非 ordered），因此原 `obj` 桶里没有
native draw jobs（`if constexpr (Ordered)` 分支不编译），1798/2335 ms 全在 128 项精灵循环；据此把拆分改为：

| 桶 | 打印名 | 覆盖代码 |
|---|---|---|
| 0 | `winobj` | 窗口寄存器读取 + OBJ window 行掩码（`mark_obj_window_scanline`，128 项 OAM） |
| 1 | `winrow` | 逐列 `window_control` 解析 + coverage 清零 |
| 2 | `prep` | BLDCNT/targets、`semi_obj_line` OAM 扫描、行缓冲、lambda |
| 3 | `bg` | 所有 BG 层（regular/affine/bitmap） |
| 4 | `fgn` | foreign presentation |
| 5 | `objscan` | 通过初始剔除的精灵的尺寸/坐标/条带计算 |
| 6 | `objfill` | 精灵像素循环（affine 与 regular 两条路径末尾各切一次）**+ 128 项里提前 return 的扫描时间**（早退无切点，落到下一个切点） |
| 7 | `out` | BLDY/混合 + RGB888 组装 + 写行 |

`render 60f` 打 total + bg/fgn/out，`render2 60f` 打 winobj/winrow/prep/objscan/objfill；两行相加即整条扫描线。

**B-1 实测（2026-11-18 17:00 日志，`窗口1_COM14_(2026-11-18-17-00-45)_ui.log`）**

> **场景更正（用户 2026-11-18 确认）**：该日志**全程没有进入赛道**，最后一直停在游戏主界面。
> 进一步：从开机起**没有任何虚拟按键操作**，游戏就停在主界面（标题画面）；只有按下 **Start 或 A**
> 才会进入「模式 / 关卡 / 人物」选择菜单。因此本日志既没有选择菜单、也没有赛道，表内所有阶段
> 都属于标题画面自身。之前按 perf 形态推断的「赛道」「菜单」标签全部作废，下表改按主界面内部
> 阶段标注；16:44 等历史日志里标「赛道」「菜单」的段落也只是推断，未在日志中验证，
> 需在明确按键操作下重取对照。

| 时段 | 主界面阶段（按 bg/OBJ 形态命名） | total | winobj | winrow | prep | bg | objscan | objfill | out |
|---|---|---|---|---|---|---|---|---|---|
| 17:01:01 | 开机/logo（bg≈0） | 2516 | 32 | 12 | 127 | 3 | 879 | 999 | 458 |
| 17:01:11 | 主界面 黑底（bg 29，objfill 峰） | 3004 | 69 | 23 | 147 | 29 | 1021 | 1199 | 511 |
| 17:01:18 | 主界面 OBJ window 打开 | 3378 | 528 | 150 | 192 | 392 | 966 | 690 | 453 |
| 17:01:26 | 同上（复现） | 3491 | 527 | 150 | 193 | 379 | 965 | 690 | 579 |
| 17:01:42 | 主界面 轻 bg（disp 主导） | 744 | 18 | 6 | 72 | 212 | 0 | 9 | 420 |
| 17:01:49 | 主界面 bg 中 | 3015 | 38 | 14 | 145 | 2056 | 0 | 92 | 656 |
| 17:02:53 | 主界面 bg 中（末态） | 3394 | 42 | 15 | 145 | 2566 | 0 | 94 | 519 |
| 17:03:03 | 主界面 bg 上升 | 3963 | 42 | 15 | 145 | 2568 | 100 | 591 | 487 |
| 17:03:24 | 主界面 bg 最重（持续到 17:07:25） | 5358 | 43 | 152 | 193 | 4073 | 198 | 227 | 457 |

**结论（B-1）**

1. **重 bg 阶段的 `bg` 是绝对主项**：4073 / 5358 = **76%**；`prep` 三桶合计 388 ms（7%），
   `out` 457 ms（8.5%），OBJ 两桶合计 425 ms（7.9%）。且该形态从 17:03:24 起连续 12 组 60f
   稳定在 `bg 4065–4074 / total 5350–5360`（抖动 <0.2%），是**可复现的静态成本** ⇒
   B-2（BG tile 行批处理）既拿大头又可用这个稳定态做量化验收。按像素折算：bg ≈424 cyc/px、
   整条 render ≈558 cyc/px，远高于 §0.7 的 104 cyc/px 硬上限。
2. **主界面内部有两条差异极大的渲染路径**：a) bg≈0–390、OBJ 主导（17:01:01–17:01:26，
   `objscan+objfill` 峰值 2200 ms = 73%）；b) bg 2.0–4.1 s、OBJ 接近零（17:01:49 之后）。
   两条路径的切换发生在同一个标题画面内，说明是标题画面自身的显示形态变化（动画帧/静态帧），
   不是场景切换。
3. **旧 `prep 854 ms` 归因关闭**：OBJ window 打开时 `winobj 528 + winrow 150 + prep 192 ≈ 870`，
   两次复现（17:01:18 / 17:01:26）；window 关闭后 `winobj 38–44 + prep 144–194`（合计 182–238 ms），
   但 17:03:24 起 `winrow` 从 14 跳到 152，原因待查（三桶合计仍只占 7%）。
4. **OBJ 口径注意**：`objfill` 含 128 项里提前 return 的扫描时间（早退无切点，落到下一个切点）。
   因此 bg 重阶段的 `objfill 92 ms` 是「逐行剔除」的固定成本而非填充；OBJ 主导阶段的 `objfill`
   才是「填充 + 其余剔除」的混合值。
5. **`out` 与阶段无关**：395–656 ms/60f（6.6–10.9 ms/帧），bg 重态占 8.5%、轻 bg 态占 21.8%，B-3 仍成立。
6. **主界面重 bg 态就是当前最慢场景**：wall 10.7–11.3 s/60f（≈5.4 fps），`halt 6.65 s`（render 5.36 s）、
   `disp 3.85–4.63 s`、`pres 0.19 s`。此前记录的 7–10 fps 没有覆盖这一状态，性能目标与验收都应以它为准。
   （`disp calls` 也从 60 升到 196–197/60f，与 `halt pumps` 42.8k–44.9k 同步，主界面每秒多约 2 次派发。）

**B-1 之后的排序**：B-2（BG，主界面重 bg 态 76%）→ 主界面 OBJ 阶段另起「每行 OAM 候选批量读取/
片上快照」（削 objscan+剔除，峰值约 2.0–2.2 s/60f）→ B-3（out 8–22%）→ B-4（泵）。
B-2 仍需先做 PC 端逐像素等价性验证。另需补两组日志：**按 Start/A 进入选择菜单**、**进入赛道**，
确认这两个场景的桶分布是否同样由 bg 主导；后续所有性能对比都要连同「按键操作路径」一起记录
（无按键 ⇒ 只有标题画面）。

### 0.16 B-2（BG）细分与回退策略

**执行纪律（用户 2026-11-18 要求）**

1. 修改点多时必须**细分**：每个子项一个独立 Kconfig 开关、一次独立提交、一段独立 plan 记录。
2. 烧录后若出现画面/行为异常，**先只关最新子项的开关**（不是整批回退），重新编译烧录确认恢复；
   确认恢复后再在该子项内部二分。只有最新子项关掉仍异常，才怀疑前一项。
3. 每个子项都必须能单独编译通过、且关闭时与上一项**字节级等价**（开关默认 off 的版本先提交一次）。
4. 验收固定三条操作路径，各自 ≥60 帧并与 §0.15 表对比：**A 标题（无按键）**、**B 选择菜单（按 Start/A）**、
   **C 赛道（选关进入）**。日志名与操作路径一并记录。

| 子项 | 内容 | 风险 | 开关 | 状态 |
|---|---|---|---|---|
| B-2a | bg 桶按层细分探针（text BG0–3 / affine BG2–3 / bitmap），仅观测，不改行为 | 无 | 随 `GBARECOMP_PERF_PROBES` | ✅ 已烧录实测（三场景数据见下） |
| B-2b | 行级「本层不可能胜出」跳过：本行所有像素的 `top.key` 已优于本层 key 且无第二候选需求时整层跳过 | 低 | `GBARECOMP_PPU_BG_LAYER_SKIP` | 📋 暂缓（满宽不透明层无跳过空间） |
| B-2c | 4bpp 行解码批量化：整 run 用一次 32 位读 + 位运算展开 8 个 nibble，去掉逐像素边界/分支 | 低 | `GBARECOMP_PPU_BG_RUN_BITS` | 🔜 下一步（text 层占 bg 64–79%） |
| B-2d | 跨帧静态行复用（标题画面 bg 恒定时直接拷贝上一帧该行） | 高（缓存一致性/存档/回放） | `GBARECOMP_PPU_BG_STATIC_ROW` | 📋 候选，默认不做 |

> 注：§0.9 已上线的 `GBARECOMP_PPU_BG_RUN_BATCH` / `BG_ZERO_RUN` / `ROW_WINDOW_CACHE` 属于已验收优化，
> 不并入本节；B-2c 只在其基础上继续压内层循环，不改它们的语义。

**B-2a 实现记录（2026-11-18）**

- [gba_ppu.cpp](file:///e:/work/Github/gba_sf32/third_party/gbarecomp-main/src/gba/gba_ppu.cpp)：新增
  `g_bg_phase_cyc[7]` / `g_bg_phase_px[7]`（0–3 = text BG0–3，4–5 = affine BG2–3，6 = bitmap BG2）与
  `PPU_BG_BEGIN/END/PX` 宏；埋点在 `render_regular_bg`（每 run 计入进入内层循环的像素数）、
  `render_affine_bg`（每行 240 px）、`render_bitmap_bg`（每行 240 px）。宏关闭时全部为空语句。
- [gba_device.cpp](file:///e:/work/Github/gba_sf32/src/gba_device.cpp)：每 60 帧追加两行
  `[gba] bgL 60f: r0 ms/px r1 … r3 …` 与 `[gba] bgX 60f: a2 … a3 … bmp …`，打印后清零。
- 编译：`main.bin` 16,370,772 B（较 B-1 +512 B），ROM 16,261,180 / 16,613,376（余 352.1 KB），
  patch 55,108 B / 9 files。
- 判读：`rN` 的 ms 之和应≈§0.15 的 `bg` 桶；`px` 明显小于 9600×240 说明被 zero-run 跳过；
  若 `a2/a3/bmp` 非零且占比大，说明标题画面走的是 affine/bitmap 路径，B-2c 的 4bpp 优化无效，应转 B-2b/B-2d。

**B-2a 实测：三场景分层（2026-11-18，`窗口1_COM14_(2026-11-18-17-34-37)_ui.log`）**

用户标注的场景切分：`17:37:26:117` 前 = 标题画面，此后 = 选择模式/关卡/人物菜单，`17:42:45:434` 后 = 赛道。
每行均为 60 帧窗口，单位 ms（`bgL/bgX` 为 ms/px）。

| 场景 | 代表时刻 | wall | disp | render | halt | bg | out | OBJ 两桶 | win+prep | fps |
|---|---|---|---|---|---|---|---|---|---|---|
| 标题（末态） | 17:37:18 | 11001 | 4038/196 | 5392 | 6748 | **4092** | 458 | 438 | 390 | 5.45 |
| 选择菜单（稳态） | 17:40:25 | 13498 | 5247/1380 | 6683 | 8034 | **4035** | 583 | 1855 | 193 | 4.45 |
| 赛道（稳态） | 17:45:31 | 17622 | **12953**/3255 | 4350 | 4434 | **2671** | 549 | 862 | 241 | 3.40 |

分层明细（ms / 进入内层循环的像素数；px/行为 60 帧折算）：

| 场景 | text BG0 | text BG1 | text BG2 | text BG3 | affine BG2 | 层和 / bg |
|---|---|---|---|---|---|---|
| 标题 | 512 / 575,520（60 px/行） | 338 / 334,224（35） | 1250 / 2,304,000（240） | 1975 / 2,304,000（240） | 0 | 4075 / 4092 |
| 菜单 | 1386 / 2,304,000（240） | 1395 / 2,304,000（240） | 0 | 0 | 1225 / 2,304,000（240） | 4006 / 4035 |
| 赛道 | 290 / 435,360（45） | 324 / 422,880（44） | 373 / 601,920（63） | 727 / 1,065,600（111） | 914 / 1,238,400（129） | 2628 / 2666 |

单像素成本（由 ms×240,000/px 折算 cyc/px）：标题 213 / 243 / **130** / **206**；菜单 **144 / 145** / – / – / **128**；
赛道 160 / 184 / 149 / **164** / **177**。

**结论（B-2a）**

1. **探针自洽**：三场景「层和 vs bg 桶」差 17–38 ms（<1.5%），且 title 的 affine/bitmap 全 0、菜单的 r2/r3 全 0、
   赛道五层同时出现，说明分层索引与埋点位置正确，可以拿来做改造前后的定量归因。
2. **text 层在所有场景都主导 bg**：标题 `r2+r3` 3225/4092 = **79%**（r3 单层 48%），菜单 `r0+r1` 2781/4035 = **69%**
   （加 affine 1225 后为 99%），赛道 text 四层 1714/2666 = **64%** + affine 914 = 34% ⇒
   **B-2c（4bpp 行解码批量化）是唯一在三场景都生效的 bg 子项**，应作为 B-2 的主实施项。
3. **B-2b（行级层跳过）优先级下调**：标题 r2/r3、菜单 r0/r1/affine 都是满宽（240 px/行）且都不透明占满，
   没有「本层全行不可能胜出」的空间；只有赛道的 r0/r1（45/44 px/行）已被现有 zero-run 削掉大半，
   再叠层跳过的收益不如批解码。
4. **内层成本与层类型弱相关**：affine 也只有 128–177 cyc/px，不比 text 便宜 ⇒ 成本在
   「取 tile 字节 → 调色板 → key/blend 判定 → 写 dst → 分支」这条通用内层路径，不在 affine 数学。
   B-2c 的收益上限约「内层压到 60–80 cyc/px」（即 bg 减半），不要把期望放在 10 倍级。
5. **赛道不是 render 主导**：`disp 12.5–13.0 s = 73%`、render 4.35 s = 25%、bg 仅占全帧 15%；
   即使 bg 减半也只从 17.6 s 降到 15.9 s（3.40 → 3.77 fps，+11%）。**赛道要提速必须回到 §0.12 的 A（AOT 指令执行）**。
6. **赛道 `disp` 随比赛推进单调上涨**：17:45:31 → 17:46:09 → 17:46:31 → 17:47:13 的 `disp` 为
   12.95 → 16.35 → 18.72 → 18.05 s，而 render 全程 4.2–4.45 s 基本不变，wall 涨到 21.2 s（2.83 fps）。
   同期 `dispatch miss ... (total)` 从 3093 涨到 5218、并出现 `08050xxx` / `0805F6xx` 新 PC ⇒
   **优先怀疑比赛逻辑/对手 AI 落进 Tier3 解释器且每次调用都解释**（§0.12 的「Tier3 不在热路径」只在开机/菜单成立）。
   需要一次 Tier3 成本探针（解释器内累计周期 / 每 60 帧）来确认，再决定是补种子还是提升解释器/派发路径。
7. **验收基线更新**（三条固定路径，替换 §0.15 的单一主界面基线）：标题 5.45 fps、菜单 4.45 fps、赛道 3.40 fps。
8. **DWT 32 位回绕（测量口径）**：60 帧窗口超过 17.9 s（= 2³²/240 MHz）时周期累加器回绕，
   `wall (X cyc)` 与任何 ≥4.29 G 的桶会**少报 17,895 ms**。已用 `disp+halt+pres ≈ wall` 交叉验证：
   17:46:31 报 `disp 829`（真值 ≈18,724）、17:47:13 报 `154`（真值 ≈18,049）。后续赛道对比必须做该校验，
   或把探针改成 64 位累加 / 每 30 帧输出。

**据此的下一步**：B-2c（独立开关 `GBARECOMP_PPU_BG_RUN_BITS`，先出开关默认 off 的等价版本）。B-2b 暂缓，B-2d 不做。
赛道侧先加 `disp` 分解探针（只观测）——已实施，见下。

**disp 分解探针（已实施，2026-11-18；已烧录实测，2026-11-19）**

依据结论 5/6：赛道 `disp` 占 73% 但内部无细分，先量再改。三处改动，全部只在 `GBARECOMP_PERF_PROBES` 打开时生效，
无开关默认行为变化：

1. `src/gba_device_stubs.cpp`：`runtime_bridge_interpret` 内加 RAII 探针（只计 Tier3 桥，不动 `runtime_tick`，
   避免每 guest 指令两次 DWT 读），产出 `g_interp_cyc` / `g_interp_calls` / `g_interp_insns`。
2. `third_party/.../runtime/runtime_bus_bridge.cpp`：只在 `runtime_tick` 的 `g_event_budget <= 0` 慢路径
   （≈每扫描线一次）计时，产出 `g_flush_cyc` / `g_flush_calls`；PPU 的 `bg`/`obj` 桶是它的子集。
3. `src/gba_device.cpp`：`g_dispatch_cycles` / `g_halt_cycles` / `g_present_cycles` 与 `to_ms()` 改 64 位，
   修掉 §0.16 结论 8 的回绕；新增一行
   `[gba] dispX 60f: interp <ms> <calls> <kinsns> | flush <ms> <flushes>`，打印后清零。

判读口径：`disp − interp − flush − memtot(rd+wr) ≈ AOT 指令执行`；
`flush − (bg+obj+out+win+prep) ≈ 设备物化开销（IRQ 入口/定时器/音频/DMA 重排）`。
编译：`main.bin` 16,371,332 B（+560 B），ROM 余 351,617 B，`scons: done building targets.`

**disp 分解探针实测（`窗口1_COM14_(2026-11-19-11-31-09)_ui.log`，64 位累加已生效）**

场景划分（用户标注）：11:36 前为开机/BIOS；11:36–12:10:06 停在标题（无按键，逐窗口统计完全一致）；
12:10:18–12:11:37 为选模式/关卡/人物的菜单；12:11:50 起赛道。

每 60 帧（ms）：

| 场景 | wall | disp (calls) | halt (pumps) | render (cyc/row) | bg | interp (calls) | flush (flushes) | memtot rd/wr |
|------|------|--------------|--------------|------------------|----|----------------|-----------------|--------------|
| 标题 11:50:01 | 10 845 | 3 856 (196) | 6 765 (43 878) | 5 394 (134 873) | 4 100 | 9 (41) | 6 631 (53 208) | 476 / 211 |
| 菜单 12:11:21 | 16 161 | 7 913 (1 311) | 8 015 (33 172) | 7 346 (183 654) | 4 128 | 35 (128) | 8 683 (50 244) | 1 034 / 674 |
| 赛道 12:13:32 | 16 520 | 11 718 (3 239) | 4 564 (27 144) | 4 177 (104 437) | 2 645 | 53 (127) | 5 899 (53 050) | 1 305 / 484 |

分层（ms/px，满宽 60 帧 = 2 304 000 px）：
- 标题：r0 514/575 520、r1 339/334 224、r2 1 250/2 304 000、r3 1 978/2 304 000，bgX 全 0
- 菜单：r0 1 400/2 304 000、r1 1 439/2 304 000、a2 1 263/2 304 000（r2/r3/a3/bmp 为 0）
- 赛道：r0 220/292 760、r1 332/422 880、r2 383/601 920、r3 738/1 065 600、a2 938/1 238 400

结论：
1. **Tier3 不是赛道瓶颈（否定结论 6 的怀疑）**：`interp` 稳态 53–167 ms（占 disp 0.5–1.5%），最坏
   762 ms / 65 kinsns（12:12:14，占 disp 7.5%、wall 4.9%）；标题 9–25 ms。⇒ 不补种子、不改桥。
2. **标题 `halt` 的 98% 是 flush**：flush 6 631 / halt 6 765，render 5 394 全在 flush 内 ⇒ 非渲染设备开销
   1.24 s/60f；disp 3.86 s 内 AOT ≈ 3.17 s（扣 memtot 0.69 s）。
3. **菜单是渲染最重的场景**：render 7.35 s（183.7k cyc/row，比标题高 36%），text r0+r1 各满宽 2.30 Mpx
   加 affine a2 满宽；disp 7.9 s、1 311 calls（标题仅 196）。
4. **赛道 disp 9.3–11.7 s（59–71% wall）**，halt 降到 4.6–5.4 s，render 4.18 s（104.4k cyc/row，比标题轻 23%）；
   flush 5 899 > halt 4 564 ⇒ 约 1.3 s flush 落在 disp 内。剩余
   `disp − interp − memtot − flush(disp 部分) ≈ 8.5 s` 全为 AOT 指令执行，是当前最大单项（52% wall）。
5. **赛道单像素成本 166–182 cyc/px**（标题 166–215 cyc/px），bg 仍 2.6–2.9 s ⇒ B-2c 收益上限约 0.3–0.6 s
   （2–4% wall），不是主项。
6. **访存比 PSRAM 原始延迟高 5–13 倍**：`mem bench` 报 sram 14.0 / psram 71.5 cyc/read；实测赛道
   IWRAM 读 421 959 次/678 ms ≈ 385 cyc、ROM 读 139 233 次/539 ms ≈ 929 cyc、写 431–672 cyc。
   guest RAM 与 cart 都在 PSRAM arena（见 `gba_device.cpp` 头注释），访存路径（区域分派 + 镜像/边界处理
   + 代码取指）本身就是主要成本。
7. **AOT 每 guest 指令成本异常高**：按每 60 帧约 16.8 M guest 周期、3.6 fps 反推约 300–400 host cyc/guest 指令
   （正常 AOT 应个位数∼数十）。最可能是「取指 + 每指令 `runtime_tick` 调用链」都落在 PSRAM/NOR
   （`mem bench` 已证明数据访问拿不到缓存收益）。此项目前只有间接推断，需要「AOT 指令数/取指模型」探针坐实后再改。
8. **验收基线（新日志）**：标题 5.3–5.5 fps、菜单 3.5–4.5 fps、赛道 3.6–4.0 fps
   （wall 10.8–11.5 / 13.2–16.9 / 15.0–16.5 s）。旧基线的赛道 17.6 s 含 DWT 回绕，已作废。

**下一步候选**（按证据强度排序）：① 查 AOT 代码段与 guest RAM 的实际放置/缓存配置（link map + SF32 缓存设置），
这是结论 6/7 的共同解释，改动可能只在配置/链接层；② 量 AOT 每指令开销（先做一次 `runtime_tick` 空转计时，
区分「取指」与「tick 调用链」）；③ B-2c 降 bg（仅 2–4%）。Tier3 相关项全部关闭。

**探针 ① 结案：缓存与 XIP 取指都是有效的（2026-11-19，`窗口1_COM14_(2026-11-19-16-46-21)_ui.log`）**

开机体检（一次，`perf_report_mpu_cache` / `perf_bench_fetch`，`GBARECOMP_PERF_PROBES`）：

```
[gba] cache: I=1 D=1 mpu_en=1 | MAIR0 77004422 MAIR1 00000033
[gba] fetch bench: call 20.06 cyc/iter, loop-only 8.03, net 12.03 (4013145 / 1607026 cyc)
[gba] mem bench sram16K 14.0 | psram128K 71.5 | psram4K 13.5 cyc/read
```

- MPU 9 个区域与 SDK `mpu_config()` 的 `__WEAK` 默认完全一致：flash `attr0`(0x22)、片上 SRAM `attr1`(0x44)、
  PSRAM `attr4`(0x33)；I-cache、D-cache、MPU 三个使能位全开。工程内无 `mpu_config()` 覆盖。
- **`fetch bench` 是决定性的**：每次「调用 flash 里的小函数」净 12 cyc（call 20.06 − 空循环 8.03）。
  若取指真是逐条裸 XIP 读（71 cyc 量级），200k 次不可能只有 3.2 M 周期 ⇒ **XIP 取指通路有效**。
- 三条 mem bench 两次开机完全一致（14.0 / 71.5 / 13.5）：PSRAM 4 KiB 工作集 13.5 vs 128 KiB 71.5
  ⇒ 数据侧缓存也在工作，只是命中率随工作集衰减。
- **结论（否定 §0.16 结论 6/7 的「缓存没生效」解释）**：`memtot` 的 191–711 cyc/次 guest 访问、以及
  每 guest 指令的高成本，不能再用「缓存配置错」解释，需从访存路径代码（区域分派/镜像/边界处理）和
  每指令 `g_cpu.R[]` 读写本身找原因。
- 保留口径：`fetch bench` 只证明 4 条指令的热循环能命中 I-cache，不能证明 13 MB `.text` 在派发跳跃下
  miss 率也低；若后续要动代码放置，需另做「跨 128 KiB 代码的取指」基准。

**菜单稳态按帧拆（同一日志 16:47:34 / 16:47:42 两个窗口，≈133 ms/帧 = 7.5 fps）**

| 项 | ms/60f | ms/帧 | 占比 | 来源 |
|---|---|---|---|---|
| AOT 指令执行（`disp − mem − interp`） | 3 268 | 54.5 | 41% | 推导 |
| bg r3（4bpp text） | 1 468 | 24.5 | 18% | `bgL` |
| `flush − render` | 1 210 | 20.2 | 15% | `dispX` − `render` |
| obj（winobj+scan+fill+out） | ~600 | 10 | 8% | `render2` |
| mem 读写 | 700 | 11.7 | 9% | `memtot` |
| bg r2 / prep / pres / halt 泵 | ~740 | 12 | 9% | 各桶 |

`flush − render` 在四个窗口稳定为 1 110–1 221 ms/60f，不是单次噪声，是此前未归因的真实开销。

**下一步变更点 1：`flush` 子桶（2026-11-19 已实现，待烧录）**

- `third_party/.../runtime/runtime_bus_bridge.cpp`：新增 `g_flush_dma_cyc`（`run_timed_dma(2)/(1)` 两处）
  与 `g_flush_recfg_cyc`（`recompute_event_budget`），都在 `GBARECOMP_PERF_PROBES` 内。
- `src/gba_device.cpp`：`dispX` 行尾追加 `| dma <ms> recfg <ms> devtick <ms>`，其中
  `devtick = flush − dma − recfg − render`（audio/timer/SIO/`ppu->tick` 的每事件开销），打印后清零。
- 判读：若 `dma` 大 ⇒ 查 HDMA/VBlank DMA 的目的地（VRAM 在 PSRAM）；若 `devtick` 大 ⇒ 查
  `ppu->tick` 与音频采样；若两者都小 ⇒ 差值落在探针未覆盖的 `mark_framebuffer_latched` 等短调用上。
- 编译：`main.bin` 16,371,220 B，ROM 余 351,824 B，RAM 228,676 / 523,264 B。

**日志清理（2026-11-19，本轮一并做掉）**

- 删除 `[gba] mpu0..mpu8` 九行区域 dump（与 SDK 默认一致，已确认、无信息量），保留一行 `cache:` 摘要。
- 删除 `mem bench` 三条与其 16 KiB 基准缓冲 `g_bench_sram`（两次结果完全一致，结论已入档；顺带 RAM 省 16 KB）。
- 更早一轮已删除 `[gba] lcd 60f`（`main.c`）、`memrd2` / `memwr2` 两条。
- 保留：`perf` / `render` / `render2` / `bgL` / `bgX` / `dispX` / `memrd` / `memwr` / `memtot` /
  `fetch bench`（1 条、仅开机）与输入 `tp ev` / `pressed`（按键映射仍待补验 B/Select/L/R）。


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
├── 3.1 固件实测占用            244,876 B（余 278,276 B，含 32KB 主线程栈）
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
  ROM 本体不占 NOR，只需装重编译代码。当前链接结果（2026-11-17，含 Phase 3.1 手柄）：
  8 shard .text 12,863,180 + dispatch 2,567,136 + BIOS/runtime/SDK ~880,000
  = Binary 16,369,908 B (15.611 MiB)  vs  分区 16,613,376 B (15.84 MiB)
  余量 353,220 B（345 KB）——已通过，无需瘦身。
  （2-D 时为 16,342,472 B / 余 270,904 B；手柄与输入代码占约 27 KB。）
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

### 2.4 屏幕方案：窗口上移，上下留作虚拟手柄

```
390 × 450 面板
┌───────────────────────┐
│  L 胶囊        R 胶囊    │ ← 上方 96 行（只放肩键）
│  ┌─────────────────┐   │
│  │                 │   │
│  │   240×160 GBA   │   │ ← y0 = 96（居中为 145，但下方要放十字键+AB，故上移）
│  │   帧缓冲         │   │
│  │                 │   │
│  └─────────────────┘   │
│  十字键   选择·开始  A    │ ← 下方 194 行（band_y0 = 281）
│           B            │
└───────────────────────┘
     ← 水平左右各 75 px
```

**实现**：LCDC ROI `(x0=75, y0=96, x1=314, y1=255)`。开机把整个面板填成 GBA 紫机身色 `RGB565(74,62,132)`
并用 `gba_pad_draw()` 画上手柄，与清屏共用同一次整屏 flush；之后只注册 240×160 窗口帧存逐帧刷新，
机身与手柄不再重绘。触摸命中由 `gba_pad_hit()` 用同一套布局常量完成（几何见 §0.10）。

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
| 2-D | 接上卡带：cart → dispatch → PPU → 送屏链路 | ✅ 真机出游戏画面（2026-11-13 12:12），miss 由 Tier3 自愈（详见 §0.3） |
| 2-E | 真实 ROM 接入：容量优化（dispatch 压缩 / -Oz / ROM LZ4）+ 端到端验证 | 📋 归档为 Phase 2-E 候选（见 §3.2.4）：ROM 尚余 353 KB，未超限，非阻塞 |
| 2-F | 按键 / 音频 / 存档 | 🔄 按键已完成（触摸虚拟手柄 → `KEYINPUT`，2026-11-17）；音频 / 存档暂不做 |

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
│   ├── SConscript          # gba_app（main.c + 宿主层 + 输入/手柄 + BIOS blob）+ gba_runtime（gbarecomp）
│   ├── main.c              # LCD 后端 + 面板几何 + gba_pad_draw() + GBA arena，调 gba_device_run()
│   ├── gba_device.cpp      # 宿主层：placement new Bus/Ppu、BIOS 装载、dispatch 主循环、set_keyinput 桥
│   ├── gba_device_stubs.cpp# overlay/dispatch-miss/未实现指令等 abort 桩
│   ├── gba_input.c / .h    # Phase 3.1：触摸线程，报点 → gba_pad_hit() → KEYINPUT
│   └── gba_pad.c / .h      # Phase 3.1：虚拟手柄绘制 + 命中判定（布局常量唯一来源）
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
- `src/SConscript`：`gba_app` 组（`main.c`、`gba_device.cpp`、`gba_device_stubs.cpp`、`gba_psram.c`、`gba_cart_sd.c`、`gba_input.c`、`gba_pad.c`、生成的 `out/gen/gba_bios_blob.c`）+ `gba_runtime` 组（`src/gba/*.cpp`、`runtime_arm.cpp`、`symbol_lookup.cpp`、`runtime_bus_bridge.cpp`、`mod_function_hooks.cpp`、`out/bios/bios_recompiled.cpp`、`bios_dispatch_table.cpp`）。BIOS 二进制在 scons 阶段转成 C 数组，免去运行时文件系统。
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
#define OFFSET_Y    96                      // 居中为 145；下方 194 行要放手柄，窗口上移

// 定稿实现（2026-11-17）：注册「窗口帧存」，每帧只推 76.8 KB，而不是整屏 351 KB
L1_NON_RET_BSS_SECT(frambuf, ALIGN(64) static uint8_t g_gba_fb[GBA_W * GBA_H * 2]);   // 逐帧写
L2_NON_RET_BSS_SECT(lcdfb, ALIGN(64) static uint8_t g_lcd_fb[LCD_W * LCD_H * 2]);     // 开机用一次

static lcd_fb_desc_t g_gba_fb_desc = {
    .area = {OFFSET_X, OFFSET_Y, OFFSET_X + GBA_W - 1, OFFSET_Y + GBA_H - 1},
    .p_data = g_gba_fb,
    .format = RTGRAPHIC_PIXEL_FORMAT_RGB565,
    .line_bytes = GBA_W * 2,
};

void render_init(void) {
    drv_lcd_fb_init("lcd");
    // 整屏铺机身色 + 画虚拟手柄（gba_pad_draw 内部整屏填充），与清屏共用一次 flush
    gba_pad_draw((uint16_t*)g_lcd_fb, LCD_W, LCD_H, OFFSET_Y, OFFSET_Y + GBA_H - 1);
    drv_lcd_fb_set(&g_panel_fb_desc);
    drv_lcd_fb_write_send(&full_area, &full_area, g_lcd_fb, NULL, 1);
    drv_lcd_fb_set(&g_gba_fb_desc);     // 之后每帧只刷窗口，机身/手柄不再重绘
}

void render_blit_gba(uint16_t* gba_pixels) {
    // 帧存地址与注册的 fb 相同 ⇒ 驱动跳过拷贝，直接送窗口
    LCD_AreaDef roi = {OFFSET_X, OFFSET_Y, OFFSET_X + GBA_W - 1, OFFSET_Y + GBA_H - 1};
    drv_lcd_fb_write_send(&roi, &roi, (uint8_t*)g_gba_fb, NULL, 1);
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
| 2.2 | 窗口区域可写 | 240×160 窗口显示测试色块 | ✅ 通过（2026-11-11 复烧确认居中 240×160 白块；Phase 3.1 起窗口上移到 y=96） |
| 2.3 | CPU 跑通 BIOS | 串口打印 BIOS 初始化日志 | ✅ 通过（236 个函数入口后进入空闲循环） |
| 2.4 | PPU 软渲染输出 | GBA 画面正确显示在窗口区域 | ✅ 通过（无卡时 forced blank 的 240×160 白块；挂 ROM 后为真实游戏画面） |
| 2.5 | 颜色格式正确 | BGR555 → RGB565 颜色无偏差 | ✅ 通过（Mario Kart 画面颜色正常，2026-11-13 起多轮真机确认） |
| 2.6 | 跑通游戏标题画面 | Mario Kart Super Circuit 标题显示 | ✅ 通过（2026-11-13 12:12 出画面；11-17 已能进选人 → 比赛） |
| 2.7 | 能操作游戏 | 触摸虚拟手柄驱动角色 | ✅ 通过（2026-11-17 16:29 Start/A/十字键，见 §0.10） |

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

### Phase 3：音频 + 输入 + 保存

> **范围决策（2026-11-17）**：先做输入，**已完成**（见 §3.3.3 与 §0.10）。音频（§3.3.1 / §3.3.2）
> 与存档（§3.3.4）明确**暂不做**——依据「性能不达标，音频和存档没有意义」。
> 下面 3.3.1 / 3.3.2 / 3.3.4 保留为设计草案，未实施。

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

#### 3.3.3 按键输入（已完成：屏幕虚拟手柄 + 触摸）

原先设想的「KEY1/KEY2 短按 + 长按凑出 A/B/Start/Select」已放弃——那样做不出十字键和 L/R。
最终方案是把整个面板当成手柄：GBA 窗口上移，上下留出的两条带子上画出十字键、AB、Select/Start、L/R，
触摸命中后写 guest 的 `KEYINPUT` 寄存器。几何与日志证据见 §0.10。

| 文件 | 职责 |
|------|------|
| `src/gba_pad.c` | 布局常量 + 绘制（整屏紫机身、灰按键，纯 rect/circle/capsule 填充）+ `gba_pad_hit()` |
| `src/gba_pad.h` | 只暴露 `gba_pad_draw()` / `gba_pad_hit()` |
| `src/gba_input.c` | 输入线程：10 ms 轮询 `"touch"` 设备的环形缓冲，UP 清 0、DOWN/MOVE 重新命中，掩码变化才写寄存器 |
| `src/gba_device.cpp` | `gba_device_set_keyinput()` → `g_bus->io().set_keyinput()`；bus 未建时丢弃掩码 |
| `src/main.c` | 开机绘制手柄并与清屏共用一次整屏 flush，之后只刷 240×160 窗口 |

**KEYINPUT 口径**：低 10 位有效、**低电平为按下**（bit0 A、1 B、2 Select、3 Start、4 Right、
5 Left、6 Up、7 Down、8 R、9 L）。`gba_pad_hit()` 返回「按下」掩码，`gba_input.c` 写成
`(~pressed) & 0x03FF`；`GbaIo::set_keyinput()` 自身再掩 `0x03FF` 并与 `synth_keyinput_`
（初值 0x03FF）按位与，所以未触摸时寄存器保持全松开。

**实测约束**：

- FT6146 滑动时重复上报 `DOWN`（`ev=2`）而不是 `MOVE`（`ev=3`），所以 DOWN/MOVE 必须走同一分支。
- 板上实体键 KEY1（PA34）/ KEY2（PA43）为 active-high，目前只打印电平，**未映射**到 GBA 按键；
  可选方案：KEY1 → Start、KEY2 → Select，作为触摸失灵时的备份。
- 按下高亮未做：`drv_lcd_fb` 只有 2 个 fb 槽位且双缓冲轮转，不能同时表示「机身」和「高亮键」，
  需要「面板级 fb + 局部 dirty rect」方案（只重传被按下/松开的那一小块）。

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

| # | 验证点 | 状态 |
|---|--------|------|
| 3.1 | DAC 输出正弦波 → 听到声音 | 🚫 暂不做 |
| 3.2 | GBA PCM DMA → 游戏音效正常 | 🚫 暂不做 |
| 3.3 | PSG 4 通道合成 → 背景音乐可听 | 🚫 暂不做 |
| 3.4 | 触摸虚拟手柄 → 能控制游戏 | ✅ Start / A / 十字键已验（2026-11-17 16:29）；B / Select / L / R 待验 |
| 3.5 | Start/Select 映射正确 | ✅ 独立圆键（(178,329) / (178,377)），不再需要长按检测；Start 已验证 |
| 3.6 | 保存/加载 → 存档成功 | 🚫 暂不做 |
| 3.7 | 完整游戏流程: 标题 → 对话 → 菜单 → 存档 → 重启 | ⏳ 部分（标题 → 选人 → 比赛已可走；存档不在范围） |

### Phase 4：完善与优化（持续）

**性能目标**：分阶段 **7 → 10 fps**（2026-11-17 实测结论：60 fps 在 240 MHz M33 上不可达，
天花板 ≈11.8 fps，见 §0.7；当前开场/菜单 6.2–7.5 / 赛道 3.3–5.9 fps）。
音频延迟、存档兼容性暂不作为目标（Phase 3 范围已裁剪）。

**优化方向**（按 §0.9 / §0.8 的实测重新排序，A→C 为当前首选）：
1. **BG 提交路径**：免写 `second[]`（A）→ out 阶段批量化（B）→ 拆 `disp` 的寄存器分配（C，Phase 2-E1）
2. **帧缓冲优化** — 已落地：LCDC 直接读 GBA 窗口帧存，无 blit（只 76.8 KB/帧，convert 165 ms + flush 22 ms per 60f）
3. **Tile 预解码** — 仅对 `fetch` 有效，而 fetch 只占 0.34 cyc/px；真正的成本在逐像素 `dec` 与 `sub`，先不做
4. **中断优先级** — LCDC 送屏与 Timer 的关系待测；音频不在范围
5. **RTOS 调度** — 当前 GUI 与仿真在同一线程串行（`wall` ≈ `disp` + `halt`），拆线程/异步送屏收益未验证
6. **EPIC（ePicasso）辅助** — 只有 out 格式转换与 OBJ 仿射能受益（无 CLUT / 无 tilemap），救不了 BG 解码主体，见 §0.8

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
| **【已解决】重编译代码体积 ≈ ROM 的 4 倍** | 已发生 | 高 | 原始 16.98MB 装不下；`GBARECOMP_SLIM` 去掉每指令 yield/trace 检查后降到 **12.68MB**，加 BIOS/runtime 与 Phase 3.1 手柄代码后当前固件 **16.37MB**，ROM 分区 16.61MB，余 **353KB** |
| gbarecomp C 代码在 Cortex-M33 上效率不够 | 已发生 | 高 | 实测开场/菜单 6.2–7.5、赛道 3.3–5.9 fps；目标改为 7→10 fps，按 §0.9 的 A/B/C 推进 |
| **【已定论】PPU 软渲染到不了 60 fps** | 已发生 | 中 | 天花板 ≈11.8 fps（§0.7）。Tile 预解码只优化 fetch（0.34 cyc/px），不能解决问题；改做逐像素 `dec`/`sub` 与 out 批量化 |
| 游戏 ROM > 8MB 放 NOR 装不下 | 低 | 中 | 自动切 TF 卡方案（当前 4MB ROM 走 TF 卡 `/gba.gba` 读入 PSRAM） |
| gbarecomp 对目标 ROM 覆盖率不足（fallback 太多） | 中 | 中 | 先用 P0/P1 游戏；manifest 反馈补全 |
| 颜色格式 BGR555 vs RGB565 色偏 | 已发生 | 低 | **已解决**：`present_frame()` 里 RGB888 → RGB565，多轮真机确认颜色正常 |
| 触摸手柄与绘制错位 / 手感偏差 | 已发生 | 低 | 命中判定复用 `gba_pad_draw()` 记录的 band 原点与 ready 标志；十字键留 ±19 死区 + 方形框便于搓对角（§0.10） |

---

## 5. 参考文件速查

### 本地 SDK
| 用途 | 路径 |
|------|------|
| SDK 根 | `e:\work\gitee\sifli\SiFli-SDK\v2.5.1\` |
| 环境脚本 | `set_env.bat` |
| 构建命令 | `scons --board=sf32lb52-lchspi-ulp -j4`。本机必须先设 `ENV_ROOT=D:\sifli\env`（否则 `scons.exe` 报 `Failed to import encodings module`），且不能省 `--board`（否则 `rtconfig has no attribute 'CHIP'`） |
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
