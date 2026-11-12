# GBA ROM 重编译到思澈黄山派（SF32LB52）

把 GBA ROM 通过 [gbarecomp](https://github.com/mstan/gbarecomp) 静态重编译成 C++20 代码，
再用 `arm-none-eabi-g++` 交叉编译到 SiFli 黄山派（SF32LB525UC6，Cortex-M33 @240MHz，RT-Thread + SiFli-SDK v2.5.1）上运行。

不涉及解释器、JIT 或反编译源码。详细方案与实测记录见 [`plan/gba_hsp_plan.md`](plan/gba_hsp_plan.md)。

---

## 1. 目录说明

| 目录 | 说明 |
|------|------|
| `plan/` | 计划与实测记录。`gba_hsp_plan.md` 是主文档 |
| `src/` | 设备端宿主层（显示后端、runtime 移植入口） |
| `roms/` | **本地素材，不入库**。放 `gba_bios.bin` 和 `.gba` ROM（需解压后的裸 ROM，不是 zip） |
| `out/` | **本地生成物，不入库**。`out/bios/` BIOS 重编译产物，`out/cart/` 原始 ROM 重编译产物，`out/cart_slim/` 瘦身版产物 |
| `third_party/` | **本地第三方源码，不入库**。按第 3 节自行获取 |
| `build_sf32lb52-lchspi-ulp_hcpu/` | scons 构建产物（不入库），含 `main.bin`、`bootloader/`、`ftab/` |
| `.trae/` | IDE / MCP 配置 |

`out/`、`roms/`、`third_party/`、`build_*/` 已在 `.gitignore` 中忽略：前者体积巨大（单个 shard 17-23MB 源码），ROM/BIOS 有版权，第三方源码自带独立版本库。

> 下文用 `<repo>` 表示本工程根目录，`<SDK_ROOT>` 表示 SiFli-SDK v2.5.1 根目录，`<toolchain>` 表示 ARM GCC 安装目录，请替换成自己的实际路径。

## 2. 硬件与目标平台

| 项 | 值 |
|------|------|
| 开发板 | 黄山派（立创），board 名 `sf32lb52-lchspi-ulp` |
| 芯片 | SF32LB525UC6：Cortex-M33 大核 @240MHz，片上 SRAM 512KB，合封 8MB OPI-PSRAM + 16MB QSPI NOR |
| 屏幕 | CO5300AF-01 AMOLED 390×450 QSPI RGB565 |
| 音频 | AU_DAC1P/N + AW8155 PA（PA_EN=PA42） |
| 按键 | KEY1=PA34，KEY2=PA43 |
| SDK | SiFli-SDK **v2.5.1**（只用 SDK，不用 Solution） |

屏幕不做缩放：240×160 居中，四周黑边（OFFSET_X=75, OFFSET_Y=145）。

内存规划：ROM 镜像（比如 4MB 的 GBA ROM）放 PSRAM；重编译代码放 NOR；WRAM/EWRAM/CPU 状态放片上 SRAM。

## 3. 获取 gbarecomp（第三方）

```bash
git clone https://github.com/mstan/gbarecomp.git third_party/gbarecomp-main
cd third_party/gbarecomp-main
git submodule update --init --recursive
```

**注意：子模块必须落在下面 pin 的 commit 上（不是各仓库的 master tip）**，否则编译报
`ArmCodegen::supports_native_region` / `CodegenCtx::native_region` 未声明（C2039）：

| 子模块 | 路径 | pin commit |
|--------|------|-----------|
| arm-recomp-core | `external/arm-recomp-core` | `63dd32dd210a8bf18bbd11933393a7b87f9b3bf6` |
| rbengine | `external/rbengine` | `2a03e73693acee0fb78076ea058642c931bef12e` |
| recomp-net | `external/recomp-net` | `8c76fb643339144a3a84d5ba8f5defe6250dcd26` |

如果 `git submodule update` 拉到的不是这些 commit，进对应子模块目录 `git checkout <sha>` 即可。

### 网络受限时的备用方案

如果 `github.com` 直连不通，可改用 GitHub API + codeload（通常可达）：

```bash
# 1) 列出仓库树，读出子模块目录的 gitlink（或从 .gitmodules / 本表取 SHA）
curl "https://api.github.com/repos/mstan/gbarecomp/git/trees/main?recursive=1"

# 2) 按 commit 下载 tar 包并解压覆盖对应子模块目录
curl -L "https://codeload.github.com/<owner>/<repo>/tar.gz/<sha>" -o sub.tar.gz
```

替换子模块后如果链接报错，**删掉整个构建目录重新 configure**（tar 解压会保留旧 mtime，ninja 会误判为 up-to-date）。

## 4. 编译

### 4.1 编译代码生成器 `gba_recompile`（Windows / MSVC）

生成器是 CMake 工程（C++），不是 Rust CLI。需要 **VS 2022 Build Tools（MSVC）+ Ninja + CMake ≥ 3.30**
（cmake 3.30 没有 "Visual Studio 18 2026" 生成器，所以用 Ninja + VS DevShell）。

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64'

$repo = "<repo>\third_party\gbarecomp-main"
cmake -S $repo -B "$repo\build" -G Ninja `
  -DGBARECOMP_TOMLPP_INCLUDE_DIR="$repo\third_party\tomlpp-src" `
  "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /utf-8" `
  "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /utf-8"
cmake --build "$repo\build" --target gba_recompile --parallel 16
```

产物：`$repo\build\gba_recompile.exe`。

改过 `arm_codegen.cpp` 等源码后，重建受影响的目标再重链即可：

```powershell
cmake --build "$repo\build" --target gbarecomp_armv4t --parallel 16
cmake --build "$repo\build" --target gba_recompile --parallel 16
```

**两个必踩的坑**：

1. **必须加 `/utf-8`**。源码是 UTF-8 无 BOM，MSVC 默认按系统代码页解析，会把注释里的 `≥`/`≤` 解码错乱、吞掉后面的枚举名，报一堆 `error C2838: 'GE': illegal qualified name in member declaration`。
2. **必须用 Ninja + VS DevShell**（见上面的生成器限制）。

### 4.2 交叉编译固件（目标板）

SDK 与环境包的安装见思澈官方文档（[SDK 文档](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/)）。装好后用 `set_env.bat gcc` 初始化 shell，在**本仓库根目录**执行：

```powershell
scons --board=sf32lb52-lchspi-ulp -j8
```

产物在 `build_sf32lb52-lchspi-ulp_hcpu/`：`main.bin`、`bootloader/bootloader.bin`、`ftab/ftab.bin`。

本项目实测版本：arm-none-eabi-gcc **14.2.1**、SCons **4.9.1**。

单文件编译验证（不依赖 scons）：

```powershell
$env:Path = "<toolchain>\bin;" + $env:Path
$repo = "<repo>\third_party\gbarecomp-main"
$cart = "<repo>\out\cart_slim"
$inc = @("-I$repo\src\armv4t","-I$repo\src\gba","-I$repo\src",
         "-I$repo\external\arm-recomp-core\profiles\armv4t_gba","-I$cart")
arm-none-eabi-g++ -std=c++20 -Os -mcpu=cortex-m33 -mthumb `
  -fno-exceptions -fno-rtti -fno-threadsafe-statics `
  -c "$cart\recompiled_000.cpp" @inc -o shard0.o
arm-none-eabi-size -A shard0.o | Select-String "^\.text"
```

> **坑：scons 命令行里的工程路径盘符必须大写**（如 `E:` 而不是 `e:`）。小写会让 `customer/boards/SConscript` 的路径包含判断失效，board 源文件被重复加入，bootloader 链接报 `multiple definition of bsp_init`。

## 5. 常用流程

```powershell
$exe = "<repo>\third_party\gbarecomp-main\build\gba_recompile.exe"

# BIOS（仓库自带审计过的 bios/gba_bios.toml）
& $exe --bios <repo>\roms\gba_bios.bin --out <repo>\out\bios

# ROM（cart 模式，默认入口 0x08000000）
& $exe --rom "<repo>\roms\xxx.gba" --out <repo>\out\cart

# 瘦身模式（体积优先，见第 6 节）
$env:GBARECOMP_SLIM = "1"
& $exe --rom "<repo>\roms\xxx.gba" --out <repo>\out\cart_slim
```

运行期仍然需要 ROM 镜像和 BIOS（`bus.rom_ptr()` / `bus.rom_size()`），ROM 镜像最终要放到 PSRAM。

其它可用参数（`--codegen-shards`、`--max-functions`、`--entry`、`--symbols`、`--config`）见 `gba_recompile --help`。

## 6. 体积约束与瘦身开关 `GBARECOMP_SLIM`

上游生成的代码为每条 guest 指令都插入 `if (runtime_should_yield()) return;` 和
`if (g_runtime_insn_trace) runtime_insn_fp();`。4MB ROM 重编译后 Cortex-M33 `.text` 约 **16.2MB**，
加 BIOS 与 runtime 超过 16MB NOR；降低优化等级无改善（`-Oz` 与 `-Os` 产出完全相同）。

**改动**：`external/arm-recomp-core/profiles/armv4t_gba/arm_codegen.cpp` 新增 `slim_mode()`，
把上述两条 emission 用 `if (!slim_mode()) { ... }` 包起来。控制流跳转仍会 `return` 回 dispatch
循环，`runtime_tick()` 的周期记账保持不变，guest 时序不受影响。

```cpp
#ifndef GBARECOMP_SLIM
#define GBARECOMP_SLIM 0
#endif

bool slim_mode() {
    static const bool enabled = []() {
        const char* env = std::getenv("GBARECOMP_SLIM");
        if (env != nullptr && *env != '\0') return std::atoi(env) != 0;
        return GBARECOMP_SLIM != 0;
    }();
    return enabled;
}
```

开关方式（默认关闭，关闭时输出与上游逐字节一致）：

| 方式 | 用法 |
|------|------|
| 编译期宏 | 构建生成器时加 `-DGBARECOMP_SLIM=1`（如追加到 `CMAKE_CXX_FLAGS`） |
| 运行期环境变量 | `$env:GBARECOMP_SLIM="1"`；`="0"` 可强制关闭（覆盖宏打开的情况） |

**语义影响**：抢占粒度从"每指令"变成"每控制流跳转"，周期计数不变。

### 6.1 实测体积分账（Mario Kart SC，4 MiB ROM）

以下数字取自 Phase 2-D 真实链接结果（`build_sf32lb52-lchspi-ulp_hcpu/main.bin` 与 `main.map`），
不是估算值。

**FLASH（NOR XIP）合计 16,342,472 B = 15.585 MiB**

| 项 | 大小 | 占比 |
|------|------|------|
| 卡带重编译代码（8 shard） | 12,821,124 B | 78.4% |
| `kDispatchTable`（派发表，12 B/条） | 2,014,920 B | 12.3% |
| native-RAM 模板表 `ram_entries`（16 B/条） | 433,760 B | 2.7% |
| SDK + 驱动 + 库 | 538,674 B | 3.3% |
| BIOS 重编译 + BIOS dispatch | 387,240 B | 2.4% |
| gbarecomp runtime + GBA machine（bus/ppu/io） | 125,679 B | 0.8% |
| 应用层（main.c / gba_cart_sd.c 等） | 约 2,200 B | <0.1% |
| **合计** | **16,342,472 B** | — |
| 分区上限（工程级 `ptab.json`） | 16,613,376 B | 余 270,904 B（264 KB） |

固定底座（SDK + BIOS + runtime）约 **0.9 MB**，与 ROM 体积基本无关；派发表 + 模板表合计
**2,448,680 B（2.34 MB）**，与重编译入口数成正比。经验公式：

```
FLASH ≈ 3.0 × ROM 体积 + 2.45 MB（派发表 / 模板表） + 0.9 MB（SDK + BIOS + runtime 底座）
```

即重编译代码本身约为 ROM 的 3 倍（4 MiB ROM → 12.2 MiB `.text`）。NOR 分区 15.84 MiB
只够一个 4 MiB 级的游戏。

**RAM**

| 区域 | 用途 | 大小 |
|------|------|------|
| PSRAM `.RW_PSRAM_NON_RET` | 卡带镜像 `g_gba_cart_image` | 4,194,304 B |
| | machine arena（`GbaBus` 407,992 + `GbaPpu` 230,496） | 1,048,576 B |
| | 冷缓冲 arena `g_gba_psram_arena` | 1,048,576 B |
| | LCD 全屏帧存 `g_lcd_fb`（390×450×2） | 351,000 B |
| | **小计** | **6,642,496 B** |
| SRAM | RT-Thread 堆（306,324 预留，实测峰值用 236,228） | — |
| | `.retm_data` 25,840 + `.data` 3,936 + `.bss` 78,572 | 约 414 KB |

重编译代码与两张派发表都在 NOR 上 XIP 执行，**不占 RAM**。RAM 占用与 ROM 体积基本无关，
唯一随 ROM 变的是 `g_gba_cart_image`（上限 4 MiB）。

可回收余量：冷缓冲 arena 1 MiB 实际只用了 14,336 B（音频采集环），machine arena 1 MiB
实需 638,488 B——两者合计可腾出约 1.4 MB PSRAM。

## 7. 注意事项

- 子模块必须用第 3 节 pin 的 commit，不是 master。
- 重新构建生成器后务必**重新生成**目标 ROM 的代码，旧产物不会自动更新。
- 生成器改动在子模块里（`external/*`），主仓库只会看到子模块指针变化。
- ROM 镜像**不入 NOR**：开机从 TF 卡根目录 `/gba.gba` 整份读入 PSRAM（上限 4 MiB，`src/gba_cart_sd.c`），再 `set_rom()` 交给 runtime。之所以必须常驻 RAM 而不是按需读卡，是因为重编译只翻译指令，图形/关卡数据运行时仍按 `0x08000000` 读，而 SPI1 12 MHz ≈ 1.2 MB/s，同步读会阻塞每帧预算。
- 子模块来源的代码是 C++20，只用 `<cstdint>`，无 STL、无内联汇编，可直接用 `arm-none-eabi-g++` 编译。
- 设备端必须让**全局** `CXXFLAGS` 带 `-std=c++20 -fexceptions -funwind-tables`：runtime 用 `throw` 退出 dispatch 循环，而 SDK 默认 `-fno-exceptions`。`SConstruct` 已在 `SifliEnv()` 后追加，`DefineGroup` 的局部 flags 只对 C 生效，改不回来。
- 设备端的功能开关（包括对第三方代码的宏控）都做成 Kconfig 选项：符号在 `Kconfig.proj` 的 `GBARECOMP runtime` 菜单里，默认值写在 `proj.conf`，需要时用 `scons --menuconfig` 改。当前 6 个选项：

  | 选项 | 默认 | 作用 |
  |------|------|------|
  | `GBARECOMP_COMPACT_FRAMEBUFFER` | y | 帧存改 240×160 `std::array`，不建 1280×864 宿主视图 |
  | `GBARECOMP_NO_HOST_SAMPLER` | y | 去掉 `std::thread` 采样线程 |
  | `GBARECOMP_SMALL_TRACE_RING` | y | trace ring 4096 → 64 条（省 288KB `.bss`） |
  | `GBARECOMP_NO_HANG_TRACE_DUMP` | y | 去掉 hang dump 的 4096×72B ring（再省 288KB） |
  | `GBARECOMP_PSRAM_ALLOC` | y | 大块冷缓冲从 PSRAM arena 分配 |
  | `GBARECOMP_SMALL_AUDIO_CAPTURE` | n | 采集环 262144 → 1024 条；无 PSRAM 的板子才需要打开 |

  gbarecomp 源码不 include `rtconfig.h`，所以 `src/SConscript` 用 `GetDepend()` 读出这些开关再映射成 `CPPDEFINES`，构建时会打印 `[gba] runtime features: ...`。更新 `third_party/gbarecomp-main` 后若这些宏分支不存在，链接会报 `region 'RAM' overflowed`。
- 大块、低频访问的运行期缓冲放 PSRAM：`src/gba_psram.c` 用 `L2_NON_RET_BSS` 静态预留 1 MiB arena（`0x60xxxxxx`）并提供 bump 分配器；`GBARECOMP_PSRAM_ALLOC` 打开后，`GbaAudio` 的采集环从这个 arena 分配（`GBARECOMP_SMALL_AUDIO_CAPTURE=y` 时约 14 KB），不再挤占约 320KB 的片上堆。片上堆只留给访问频繁的小缓冲（音频输出 ring、BIOS、临时对象）。两种分配启动时都会打印用量，便于判断是否放错位置。
- BIOS 由 `src/SConscript` 在构建时转成 C 数组（`out/gen/gba_bios_blob.c`），因此 `roms/gba_bios.bin` 必须在编译前就位。
