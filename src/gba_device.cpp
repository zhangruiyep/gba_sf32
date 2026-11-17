// gba_device.cpp — device-side host layer around the recompiled GBA runtime.
//
// Phase 2-D: boot the guest BIOS from its reset vector, hand over to the
// cartridge image read from the TF card, and keep dispatching recompiled cart
// code. With no card present the firmware falls back to the Phase 2-C
// behaviour (BIOS only, stop at the first cart fetch).
//
// The GBA machine objects are constructed with placement new inside a PSRAM
// arena supplied by main.c: GbaBus owns ~386 KB of inline guest RAM and
// GbaPpu owns two 240x160 RGB888 framebuffers, neither of which fits in the
// 512 KB of on-chip SRAM.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>

#include <rtthread.h>

#include "runtime_arm.h"          // g_cpu, runtime_dispatch, g_runtime_fn_entry_hook
#include "runtime_bus_bridge.h"   // gbarecomp::set_active_bus / set_active_ppu
#include "gba_bios.h"
#include "gba_bus.h"
#include "gba_ppu.h"

// Baked into the image by src/SConscript from roms/gba_bios.bin.
extern "C" const uint8_t  g_gba_bios_blob[];
extern "C" const uint32_t g_gba_bios_blob_size;

// src/gba_cart_sd.c: cartridge image read from the TF card into PSRAM.
extern "C" const uint8_t *gba_cart_image(void);
extern "C" std::size_t    gba_cart_load(const char *path);

// The SDK start-up code never calls __libc_init_array(), so C++ file-scope
// constructors are dead by default. The recompiled cartridge uses one to
// register its native-RAM code templates with the dispatcher.
extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);

// main.c: convert an RGB888 GBA frame and push it to the panel.
extern "C" void gba_lcd_present(const uint8_t *rgb888, uint32_t width, uint32_t height);

// Defined in runtime_bus_bridge.cpp, but not exposed by a header.
void runtime_set_frame_present_hook(std::function<bool()> hook);
void runtime_mark_present_stack_base();

namespace {

constexpr uint32_t kCartBase = 0x08000000u;
constexpr uint32_t kBiosSize = 16u * 1024u;
constexpr const char *kCartPath = "/gba.gba";

gba::GbaBus  *g_bus = nullptr;
gba::GbaPpu  *g_ppu = nullptr;
gba::GbaBios  g_bios;

// Temporary performance probe (DWT cycle counter). Delete this block and the
// [gba] perf line with it once the frame-rate question is answered. The core
// clock is assumed to be 240 MHz; the report also prints the raw DWT cycle
// count for the window, so a wrong assumption shows up as a mismatch against
// the rt_tick-based wall time.
constexpr uint32_t kCoreHz = 240000000u;

// GBARECOMP_PERF_PROBES (Kconfig.proj) gates the DWT timing probes. With the
// option off the clock is never started and every reading is 0, so the probe
// call sites below stay in place but compile to nothing.
inline uint32_t perf_now(void)
{
#if defined(GBARECOMP_PERF_PROBES)
    return *(volatile uint32_t *)0xE0001004u;          // DWT->CYCCNT
#else
    return 0u;
#endif
}

void perf_start(void)
{
#if defined(GBARECOMP_PERF_PROBES)
    *(volatile uint32_t *)0xE000EDFCu |= (1u << 24);   // DEMCR.TRCENA
    *(volatile uint32_t *)0xE0001004u = 0;
    *(volatile uint32_t *)0xE0001000u |= 1u;           // DWT->CTRL.CYCCNTENA
#endif
}

uint32_t g_dispatch_cycles = 0;
uint32_t g_dispatch_calls = 0;
uint32_t g_halt_cycles = 0;
uint32_t g_halt_pumps = 0;
uint32_t g_present_cycles = 0;
uint32_t g_present_count = 0;
uint32_t g_report_tick = 0;
uint32_t g_report_cycle = 0;

// Device-phase buckets filled by runtime_bus_bridge.cpp's tick_devices().
// Temporary: delete together with its `[gba] perf2` print line.
#if defined(GBARECOMP_PERF_PROBES)
extern "C" {
extern unsigned g_dev_prof_cyc[8];
extern unsigned g_dev_prof_renders;
extern unsigned g_dev_prof_loops;
extern unsigned g_ppu_phase_cyc[5];
extern unsigned g_ppu_bg_layer_rows;
extern unsigned g_ppu_bg_runs;
extern unsigned g_ppu_bg_px;
extern unsigned g_ppu_bg_subm;
extern unsigned g_ppu_bg_fetch_cyc;
extern unsigned g_ppu_bg_pix_cyc;
extern unsigned g_ppu_bg_fast_px;
extern unsigned g_ppu_bg_fast_subm;
}
#endif

uint32_t to_ms(uint32_t cycles)
{
    return (uint32_t)(((std::uint64_t)cycles * 1000u) / kCoreHz);
}

void perf_report(void)
{
#if defined(GBARECOMP_PERF_PROBES)
    const uint32_t wall_ms = rt_tick_get() - g_report_tick;
    const uint32_t wall_cyc = perf_now() - g_report_cycle;

    rt_kprintf("[gba] perf 60f: wall %u ms (%u cyc) | disp %u ms %u calls | halt %u ms %u pumps | pres %u ms %u\n",
               (unsigned int)wall_ms, (unsigned int)wall_cyc,
               (unsigned int)to_ms(g_dispatch_cycles), (unsigned int)g_dispatch_calls,
               (unsigned int)to_ms(g_halt_cycles), (unsigned int)g_halt_pumps,
               (unsigned int)to_ms(g_present_cycles), (unsigned int)g_present_count);

    // Device-pump split. `render` is the scanline compositor; `qry` the
    // event-distance queries; anything left between `halt` above and their sum
    // is runtime_tick()/tick_devices bookkeeping plus DMA steal and IRQ entry.
    const uint32_t rend_cyc = g_dev_prof_cyc[4];
    rt_kprintf("[gba] perf2 60f: render %u ms %u ln (%u cyc/ln) | ppu %u | audio %u | tim %u | sio %u | hdma %u | vbl %u | qry %u | loops %u\n",
               (unsigned int)to_ms(rend_cyc), (unsigned int)g_dev_prof_renders,
               (unsigned int)(g_dev_prof_renders != 0u
                                  ? rend_cyc / g_dev_prof_renders : 0u),
               (unsigned int)to_ms(g_dev_prof_cyc[3]),
               (unsigned int)to_ms(g_dev_prof_cyc[0]),
               (unsigned int)to_ms(g_dev_prof_cyc[1]),
               (unsigned int)to_ms(g_dev_prof_cyc[2]),
               (unsigned int)to_ms(g_dev_prof_cyc[5]),
               (unsigned int)to_ms(g_dev_prof_cyc[6]),
               (unsigned int)to_ms(g_dev_prof_cyc[7]),
               (unsigned int)g_dev_prof_loops);

    // Scanline phase split (see g_ppu_phase_cyc in gba_ppu.cpp): prep / BG /
    // foreign / OBJ / output. Temporary like the counters above.
    rt_kprintf("[gba] perf3 60f: prep %u | bg %u | fgn %u | obj %u | out %u ms\n",
               (unsigned int)to_ms(g_ppu_phase_cyc[0]),
               (unsigned int)to_ms(g_ppu_phase_cyc[1]),
               (unsigned int)to_ms(g_ppu_phase_cyc[2]),
               (unsigned int)to_ms(g_ppu_phase_cyc[3]),
               (unsigned int)to_ms(g_ppu_phase_cyc[4]));
    const uint32_t bg_phase_cyc = g_ppu_phase_cyc[1];
    for (unsigned int i = 0; i < 5u; ++i)
    {
        g_ppu_phase_cyc[i] = 0;
    }

    // Text-BG work counters (see g_ppu_bg_* in gba_ppu.cpp): divides the `bg`
    // bucket by actual work so an unexpectedly high cycles/pixel shows up as a
    // per-unit cost rather than a guess. Kept short enough for the ~128-column
    // console, so the derived ratios are left to the log reader.
    {
        const uint32_t bg_cyc = bg_phase_cyc;
        rt_kprintf("[gba] perf4 60f: rows %u runs %u px %u subm %u | bg %u fetch %u pix %u cyc\n",
                   (unsigned int)g_ppu_bg_layer_rows,
                   (unsigned int)g_ppu_bg_runs,
                   (unsigned int)g_ppu_bg_px,
                   (unsigned int)g_ppu_bg_subm,
                   (unsigned int)bg_cyc,
                   (unsigned int)g_ppu_bg_fetch_cyc,
                   (unsigned int)g_ppu_bg_pix_cyc);
        g_ppu_bg_layer_rows = 0;
        g_ppu_bg_runs = 0;
        g_ppu_bg_px = 0;
        g_ppu_bg_subm = 0;
        g_ppu_bg_fetch_cyc = 0;
        g_ppu_bg_pix_cyc = 0;
    }

    // Coverage of the 4bpp/no-window fast path in the BG run loop (see
    // g_ppu_bg_fast_px in gba_ppu.cpp). Compare against `px`/`subm` in perf4:
    // if the fast path covers most pixels, the per-pixel work is what it is;
    // if it does not, the generic (windowed / 256-color) loop is the hot one.
    rt_kprintf("[gba] bgfast 60f: px %u subm %u\n",
               (unsigned int)g_ppu_bg_fast_px,
               (unsigned int)g_ppu_bg_fast_subm);
    g_ppu_bg_fast_px = 0;
    g_ppu_bg_fast_subm = 0;

    g_report_tick = rt_tick_get();
    g_report_cycle = perf_now();
    g_dispatch_cycles = 0;
    g_dispatch_calls = 0;
    g_halt_cycles = 0;
    g_halt_pumps = 0;
    g_present_cycles = 0;
    g_present_count = 0;
    for (unsigned int i = 0; i < 8u; ++i)
    {
        g_dev_prof_cyc[i] = 0;
    }
    g_dev_prof_renders = 0;
    g_dev_prof_loops = 0;
#endif
}

// Temporary: random-read latency of on-chip SRAM vs the PSRAM arena. Every
// index depends on the previous byte, so the loop measures full load latency
// instead of overlapping loads. Windows are 16 KiB (SRAM) and 128 KiB (PSRAM):
// a working data cache would drag the PSRAM figure down toward the SRAM one,
// so this also tests whether a data cache is in effect at all.
#if defined(GBARECOMP_PERF_PROBES)
uint8_t g_bench_sram[16u * 1024u];
#endif

void perf_bench_memory(const void *psram)
{
#if defined(GBARECOMP_PERF_PROBES)
    const uint8_t *area[2] = {g_bench_sram, (const uint8_t *)psram};
    const uint32_t mask[2] = {(16u * 1024u) - 1u, (128u * 1024u) - 1u};
    const char *name[2] = {"sram16K", "psram128K"};

    for (unsigned int a = 0; a < 2u; ++a)
    {
        const uint8_t *p = area[a];
        uint32_t idx = 1u;
        uint32_t sink = 0u;
        const uint32_t t0 = perf_now();
        for (uint32_t i = 0; i < 16384u; ++i)
        {
            sink += p[(idx ^ (idx >> 13)) & mask[a]];
            idx = idx * 1103515245u + sink + 12345u;
        }
        const uint32_t dt = perf_now() - t0;
        rt_kprintf("[gba] mem bench %s: %u cyc/16K reads = %u.%u cyc/read (%u)\n",
                   name[a], (unsigned int)dt,
                   (unsigned int)(dt / 16384u),
                   (unsigned int)(((dt % 16384u) * 10u) / 16384u),
                   (unsigned int)sink);
    }
#else
    (void)psram;
#endif
}

// One bit per 4-byte BIOS word, so the entry count below reports distinct
// functions instead of every call.
uint8_t  g_bios_seen[kBiosSize / 4u / 8u];
uint32_t g_bios_entries = 0;
uint32_t g_frames = 0;
bool     g_cart_entered = false;
unsigned int g_presented_vblank = 0;

void run_static_initializers(void)
{
    for (void (**fn)(void) = __init_array_start; fn < __init_array_end; ++fn)
    {
        if (*fn != nullptr)
        {
            (*fn)();
        }
    }
}

// Log the cartridge header so a wrong file on the card is obvious.
void log_cart_header(const uint8_t *rom, std::size_t size)
{
    char title[13];
    char code[5];

    if (size < 0xC0)
    {
        return;
    }

    std::memcpy(title, rom + 0xA0, 12);
    std::memcpy(code, rom + 0xAC, 4);
    title[12] = '\0';
    code[4] = '\0';
    rt_kprintf("[gba] cart: \"%s\" %s, %u B\n", title, code, (unsigned int)size);
}

void bios_entry_hook(uint32_t pc)
{
    if (pc >= kBiosSize)
    {
        // The hook also fires from every generated cartridge function, so this
        // proves whether the guest ever got past the BIOS hand-off.
        if (pc >= kCartBase && !g_cart_entered)
        {
            g_cart_entered = true;
            rt_kprintf("[gba] cart entered: pc=%08x psr=%08x mode=%02x T=%u\n",
                       pc, g_cpu.cpsr, (unsigned int)(g_cpu.cpsr & 0x1Fu),
                       (unsigned int)((g_cpu.cpsr >> 5) & 1u));
        }
        return;
    }

    const uint32_t bit = pc >> 2;
    const uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (g_bios_seen[bit >> 3] & mask)
    {
        return;
    }

    // Only the distinct-entry count is kept; per-function traces are far too
    // noisy to leave enabled once the boot path is stable.
    g_bios_seen[bit >> 3] |= mask;
    ++g_bios_entries;
}

// Mirrors reset_recomp_cpu() in the host-only runtime.cpp.
void reset_cpu(void)
{
    runtime_trace_reset();
    for (int i = 0; i < 16; ++i)
    {
        g_cpu.R[i] = 0;
    }
    for (int i = 0; i < ARM_BANK_COUNT; ++i)
    {
        g_cpu.banked_sp[i] = 0;
        g_cpu.banked_lr[i] = 0;
        g_cpu.banked_spsr[i] = 0;
    }
    for (int i = 0; i < 5; ++i)
    {
        g_cpu.r8_12_user[i] = 0;
        g_cpu.r8_12_fiq[i] = 0;
    }
    g_cpu.R[13] = 0x03007FE0u;
    g_cpu.cpsr = CPSR_I_BIT | CPSR_F_BIT | 0x13u;   // Supervisor, IRQ/FIQ masked
    g_cpu.banked_sp[ARM_BANK_SUPERVISOR] = 0x03007FE0u;
    g_cpu.banked_sp[ARM_BANK_IRQ]        = 0x03007FA0u;
    g_cpu.banked_sp[ARM_BANK_USER]       = 0x03007F00u;
}

// Called at VBlank start with the guest stack left in place, and from the host
// loop after each step. One panel update per VBlank, whoever gets there first.
bool present_frame(void)
{
    if (g_ppu == nullptr || !g_ppu->has_latched_framebuffer())
    {
        return false;
    }

    const unsigned int vbl = (unsigned int)g_runtime_vblank_starts;
    if (vbl == g_presented_vblank)
    {
        return false;
    }
    g_presented_vblank = vbl;

    const uint32_t present_start = perf_now();
    gba_lcd_present(g_ppu->latched_framebuffer(),
                    g_ppu->render_width(), g_ppu->render_height());
    g_present_cycles += perf_now() - present_start;
    ++g_present_count;

    if ((++g_frames % 60u) == 0u)
    {
        perf_report();
    }
    return false;
}

}  // namespace

extern "C" void gba_device_run(void *arena, unsigned int arena_bytes)
{
    const uintptr_t base = (reinterpret_cast<uintptr_t>(arena) + 15u) & ~uintptr_t(15u);
    const std::size_t need = sizeof(gba::GbaBus) + sizeof(gba::GbaPpu);

    if (arena_bytes < need + 15u)
    {
        rt_kprintf("[gba] arena too small, need %u B\n", (unsigned int)(need + 15u));
        return;
    }

    g_bus = new (reinterpret_cast<void *>(base)) gba::GbaBus();
    g_ppu = new (reinterpret_cast<void *>(base + sizeof(gba::GbaBus))) gba::GbaPpu();
    run_static_initializers();

    std::string err;
    if (!g_bios.load_from_bytes(g_gba_bios_blob, g_gba_bios_blob_size,
                                gba::GbaBios::kExpectedSha1, &err))
    {
        rt_kprintf("[gba] BIOS rejected: %s\n", err.c_str());
        return;
    }

    const std::size_t cart_size = gba_cart_load(kCartPath);
    const uint8_t *cart = (cart_size != 0) ? gba_cart_image() : nullptr;
    if (cart != nullptr)
    {
        log_cart_header(cart, cart_size);
    }
    else
    {
        rt_kprintf("[gba] no cartridge, running the BIOS alone\n");
    }

    // set_rom() scans the whole image for MBC/EEPROM/gyro tables, which is the
    // dominant part of boot latency on a 4 MiB cartridge.
    const rt_uint32_t bus_tick = rt_tick_get();
    g_bus->set_bios(&g_bios);
    g_bus->set_rom(cart, cart_size);
    rt_kprintf("[gba] bus bind (bios+rom) took %u ms\n",
               (unsigned int)(rt_tick_get() - bus_tick));
    g_bus->io().set_ppu(g_ppu);
    g_bus->io().set_bus(g_bus);

    gbarecomp::set_active_bus(g_bus);
    gbarecomp::set_active_ppu(g_ppu);
    runtime_init(g_bus);

    g_runtime_fn_entry_hook = &bios_entry_hook;
    runtime_set_frame_present_hook(&present_frame);
    runtime_mark_present_stack_base();
    reset_cpu();

    perf_start();
    g_report_tick = rt_tick_get();
    g_report_cycle = perf_now();

    // Temporary: SRAM vs PSRAM random-read latency, using the tail of the arena
    // (past the machine objects) as the PSRAM window.
#if defined(GBARECOMP_PERF_PROBES)
    if (arena_bytes >= need + 15u + (128u * 1024u))
    {
        perf_bench_memory(reinterpret_cast<const void *>(base + need));
    }
#endif

    rt_kprintf("[gba] BIOS loaded, booting from reset vector %08x\n", g_cpu.R[15]);

    for (;;)
    {
        const uint32_t step_start = perf_now();

        if (g_bus->io().halted())
        {
            // HALTed CPU: there is no code to run until an IRQ arrives, and the
            // IRQ can only arrive if time advances. Re-dispatching the halt
            // continuation point would spin forever (the BIOS parks at 0x348
            // right before the cartridge hand-off). Mirror pump_idle() from the
            // host runner: advance to the next device event so VBlank vectors.
            uint32_t chunk = g_ppu->cycles_until_next_event();
            const uint32_t until_timer = g_bus->io().cycles_until_next_timer_event();
            const uint32_t until_sample = g_bus->audio().cycles_until_next_shared_event();
            if (until_timer < chunk)
            {
                chunk = until_timer;
            }
            if (until_sample < chunk)
            {
                chunk = until_sample;
            }
            if (chunk == 0u || chunk == 0xFFFFFFFFu)
            {
                chunk = 1u;
            }
            runtime_tick(chunk);
            g_halt_cycles += perf_now() - step_start;
            ++g_halt_pumps;
        }
        else
        {
            const uint32_t pc = g_cpu.R[15];
            if ((cart == nullptr) && (pc >= kCartBase))
            {
                break;
            }
            runtime_dispatch(pc);
            g_dispatch_cycles += perf_now() - step_start;
            ++g_dispatch_calls;
        }

        // Slim codegen strips runtime_should_yield() from the cartridge, so the
        // guest-side frame hook never fires for cart code: present from here.
        present_frame();
    }

    rt_kprintf("[gba] BIOS left the ROM: pc=%08x, %u BIOS entries traced, %u frames\n",
               g_cpu.R[15], g_bios_entries, g_frames);
    rt_kprintf("[gba] halted (no cartridge mapped)\n");
}
