# GBA ROM 重编译到思澈黄山派（SF32LB52）

把 GBA ROM 通过 [gbarecomp](https://github.com/mstan/gbarecomp) 静态重编译成 C++20 代码，
再用 `arm-none-eabi-g++` 交叉编译到 SiFli 黄山派（SF32LB525UC6，Cortex-M33 @240MHz，RT-Thread + SiFli-SDK v2.5.1）上运行。

不涉及解释器、JIT 或反编译源码。

---

## 1. 目录说明

| 目录 | 说明 |
|------|------|
| `plan/` | 计划与实测记录。`gba_hsp_plan.md` 是主文档，含硬件确认、技术方案、阶段划分、踩坑记录、体积实测 |
| `roms/` | **本地素材，不入库**。放 `gba_bios.bin` 和 `.gba` ROM（需解压后的裸 ROM，不是 zip） |
| `out/` | **本地生成物，不入库**。`out/bios/` BIOS 重编译产物，`out/cart/` 原始 ROM 重编译产物，`out/cart_slim/` 瘦身版产物 |
| `third_party/` | **本地第三方源码，不入库**。按第 3 节自行获取 gbarecomp |
| `.trae/` | IDE / MCP 配置 |

`out/`、`roms/`、`third_party/` 已在 `.gitignore` 中忽略：前者体积巨大（单个 shard 17-23MB 源码），ROM/BIOS 有版权，第三方源码自带独立版本库。

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

## 4. 编译环境

### 4.1 编译代码生成器 `gba_recompile`（Windows / MSVC）

生成器是 CMake 工程（C++），不是 Rust CLI。需要 **VS 2022 Build Tools（MSVC）+ Ninja + CMake ≥ 3.30**。

> cmake 3.30 没有 "Visual Studio 18 2026" 生成器，所以用 Ninja + VS DevShell。

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

两个必踩的坑：

1. **必须加 `/utf-8`**。源码是 UTF-8 无 BOM，MSVC 默认按系统代码页解析，会把注释里的 `≥`/`≤` 解码错乱、吞掉后面的枚举名，报一堆 `error C2838: 'GE': illegal qualified name in member declaration`。
2. **必须用 Ninja + VS DevShell**（见上面的生成器限制）。

改过 `arm_codegen.cpp` 等源码后，重建受影响的目标再重链即可：

```powershell
cmake --build "$repo\build" --target gbarecomp_armv4t --parallel 16
cmake --build "$repo\build" --target gba_recompile --parallel 16
```

### 4.2 交叉编译重编译产物（目标板）

从 SiFli 官方获取 **SiFli-SDK v2.5.1** 及配套的 SiFli 环境包（内含 SCons、CMake、`arm-none-eabi-gcc`），
解压到任意目录，用环境包里的 `set_env.bat gcc` 初始化 shell 后再运行 scons。本工程实测可用版本：

| 工具 | 版本 / 说明 |
|------|------|
| arm-none-eabi-gcc/g++ | **14.2.1**（位置：`<toolchain>/bin`） |
| SCons | 4.9.1 |
| SiFli 环境脚本 | `set_env.bat gcc`（在 SiFli-SDK 环境目录下，提供 ENV_ROOT/ENV_VER/toolchain） |
| 烧录 | `ImgDownUart.exe`（SDK `tools/uart_download/`），波特率 1Mbps，device `SF32LB52X_NOR`，串口用板上实际枚举的端口 |

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

## 6. 对 gbarecomp 的修改记录

### 6.1 `GBARECOMP_SLIM` —— 体积瘦身开关

**文件**：`third_party/gbarecomp-main/external/arm-recomp-core/profiles/armv4t_gba/arm_codegen.cpp`

**动机**：上游生成的代码为每条 guest 指令都插入 `if (runtime_should_yield()) return;` 和
`if (g_runtime_insn_trace) runtime_insn_fp();`。一个 4MB 的 GBA ROM 重编译后
Cortex-M33 `.text` 达 **16,982,176 B（16.2MB）**，加上 BIOS 与 runtime 超过 16MB NOR，装不下；
降低优化等级无改善（`-Oz` 与 `-Os` 产出完全相同），说明问题在代码结构而不是编译器。

**改动**：新增 `slim_mode()`（匿名命名空间内，`arm_codegen.cpp` 顶部），在 `emit_reference_instr`
里把上述两条 emission 用 `if (!slim_mode()) { ... }` 包起来。控制流跳转仍会 `return` 回 dispatch
循环，runtime 仍能在那里让出 CPU；`runtime_tick()` 每条指令的周期记账**保持不变**，guest 时序不受影响。

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

**开关方式（默认关闭，关闭时输出与上游逐字节一致）**：

| 方式 | 用法 |
|------|------|
| 编译期宏 | 构建生成器时加 `-DGBARECOMP_SLIM=1`（如追加到 `CMAKE_CXX_FLAGS`） |
| 运行期环境变量 | `$env:GBARECOMP_SLIM="1"`；`="0"` 可强制关闭（覆盖宏打开的情况） |

**实测收益**（Mario Kart Super Circuit，4MB ROM，`-Os -mcpu=cortex-m33 -mthumb`）：

| 指标 | 原始 | slim | 变化 |
|------|------|------|------|
| 8 个 shard `.text` 合计 | 16,982,176 | 12,675,432 | -25.4% |
| 单 shard 源码行数 | 481,993 | 432,762 | -10.2% |
| 单 shard 编译耗时 | 49.9s | 37.9s | -24% |

固件预算：12.09MB（游戏）+ 0.35MB（BIOS）+ ~1MB（runtime）≈ 13.4MB，NOR 可用约 15.7MB，余量约 2MB。

**语义影响**：抢占粒度从"每指令"变成"每控制流跳转"，周期计数不变。绝大多数游戏无影响，但需在真机确认。

**验证方式**：

```powershell
& $exe --bios <repo>\roms\gba_bios.bin --out <repo>\out\_a          # 默认：产物含 4330 处 yield 检查
$env:GBARECOMP_SLIM="1"; & $exe --bios <repo>\roms\gba_bios.bin --out <repo>\out\_b
$env:GBARECOMP_SLIM=$null                                          # 该产物应为 0 处
```

### 6.2 注意事项

- 改动在子模块里（`external/*`），主仓库只会看到子模块指针变化，改动本体在子模块自己的版本库里。
- 重新构建生成器后务必**重新生成**目标 ROM 的代码，旧产物不会自动更新。

## 7. 当前状态

| 阶段 | 状态 |
|------|------|
| Phase 0：SDK 环境 + LCD 点亮 | 已完成（串口打印 `CO5300_ReadID 0x331100`，屏幕点亮） |
| Phase 1：PC 端重编译产出 C++ 代码 | 已完成（BIOS 735 函数 / ROM 19,804 函数 / 4.27M 行；ARM 编译零错误；体积已瘦身到装得下） |
| Phase 2：思澈平台移植（CPU + LCD） | 未开始 |
