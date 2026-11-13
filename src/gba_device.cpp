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

// RT-Thread's operator new returns nullptr instead of throwing, so an oversized
// std::vector inside the runtime shows up as a null-pointer fault with no hint.
// Print the heap high-water mark around each bring-up step.

// src/gba_psram.c: external PSRAM arena that backs the runtime's cold buffers.
extern "C" std::size_t gba_psram_used(void);
extern "C" std::size_t gba_psram_capacity(void);

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

// Depth of IRQ handlers currently on the host stack (runtime_arm.cpp). The
// frame-present gate inside runtime_should_yield() requires it to be zero, so
// a stuck non-zero value silently disables presentation.
extern "C" uint32_t g_irq_nest_depth;

namespace {

constexpr uint32_t kCartBase = 0x08000000u;
constexpr uint32_t kBiosSize = 16u * 1024u;
constexpr const char *kCartPath = "/gba.gba";

gba::GbaBus  *g_bus = nullptr;
gba::GbaPpu  *g_ppu = nullptr;
gba::GbaBios  g_bios;

// One bit per 4-byte BIOS word: each BIOS function entry is logged once, and
// the trace doubles as a coverage summary when boot ends.
uint8_t  g_bios_seen[kBiosSize / 4u / 8u];
uint32_t g_bios_entries = 0;
uint32_t g_frames = 0;

// ── Phase 2-D bring-up probes (temporary) ───────────────────────────────
// The frame-present hook is only reachable from runtime_should_yield(), which
// slim codegen strips from the cartridge entirely, so guest code can never
// present a cart frame. These counters show whether the guest is advancing:
//   cart_ent -> does the guest ever execute cartridge code (entry hook)
//   disp     -> does control return to the host dispatch loop
//   idle     -> host-side HALT pumps (guest has no code to run until an IRQ)
//   hal/irqd -> is the CPU parked in HALT, or is an IRQ chain stuck on our stack
uint32_t g_cart_entries = 0;
uint32_t g_dispatch_returns = 0;
uint32_t g_idle_pumps = 0;
rt_uint32_t g_last_status_tick = 0;
unsigned int g_last_status_vblank = 0;
unsigned int g_presented_vblank = 0;

void debug_status(void *parameter)
{
    (void)parameter;

    const rt_uint32_t now = rt_tick_get();
    const unsigned int vbl = (unsigned int)g_runtime_vblank_starts;
    const uint32_t psr = g_cpu.cpsr;

    gbarecomp::RuntimeTimingContext timing;
    gbarecomp::runtime_capture_timing_context(timing);

    // rt_kprintf has no %ll, so every 64-bit counter is narrowed to 32 bits;
    // all of these stay far below 2^32 during a bring-up window.
    rt_kprintf("[gba] dbg t=%u pc=%08x psr=%08x mode=%02x T=%u vbl=%u(+%u) cyc=%u irqd=%u hal=%u cart_ent=%u disp=%u idle=%u frame=%u\n",
               (unsigned int)now, g_cpu.R[15], psr,
               (unsigned int)(psr & 0x1Fu), (unsigned int)((psr >> 5) & 1u),
               vbl, vbl - g_last_status_vblank,
               (unsigned int)timing.cycles, (unsigned int)g_irq_nest_depth,
               (unsigned int)(g_bus->io().halted() ? 1u : 0u),
               (unsigned int)g_cart_entries, (unsigned int)g_dispatch_returns,
               (unsigned int)g_idle_pumps, (unsigned int)g_frames);

    g_last_status_tick = now;
    g_last_status_vblank = vbl;
}

// Runs on the soft-timer thread, so it keeps reporting even when the guest
// never yields back to the host loop.
void debug_status_start(void)
{
    rt_timer_t timer = rt_timer_create("gba_dbg", &debug_status, RT_NULL, 1000u,
                                       RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);
    if (timer == RT_NULL)
    {
        rt_kprintf("[gba] dbg timer create failed\n");
        return;
    }

    g_last_status_tick = rt_tick_get();
    g_last_status_vblank = g_runtime_vblank_starts;
    rt_timer_start(timer);
}

void print_heap(const char *stage)
{
    rt_uint32_t total = 0;
    rt_uint32_t used = 0;
    rt_uint32_t max_used = 0;

    rt_memory_info(&total, &used, &max_used);
    rt_kprintf("[gba] heap %s: total=%u used=%u max=%u\n",
               stage, total, used, max_used);
    rt_kprintf("[gba] psram %s: used=%u of %u\n", stage,
               (unsigned int)gba_psram_used(),
               (unsigned int)gba_psram_capacity());
}

void run_static_initializers(void)
{
    uint32_t count = 0;

    for (void (**fn)(void) = __init_array_start; fn < __init_array_end; ++fn)
    {
        if (*fn != nullptr)
        {
            (*fn)();
            ++count;
        }
    }
    rt_kprintf("[gba] static initializers: %u\n", (unsigned int)count);
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
        if (pc >= kCartBase)
        {
            if (g_cart_entries == 0u)
            {
                rt_kprintf("[gba] cart entered: pc=%08x psr=%08x mode=%02x T=%u\n",
                           pc, g_cpu.cpsr, (unsigned int)(g_cpu.cpsr & 0x1Fu),
                           (unsigned int)((g_cpu.cpsr >> 5) & 1u));
            }
            ++g_cart_entries;
        }
        return;
    }

    const uint32_t bit = pc >> 2;
    const uint8_t mask = (uint8_t)(1u << (bit & 7u));
    if (g_bios_seen[bit >> 3] & mask)
    {
        return;
    }

    g_bios_seen[bit >> 3] |= mask;
    if (++g_bios_entries <= 256u)
    {
        rt_kprintf("[gba] bios enter %08x\n", pc);
    }
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

    if ((++g_frames % 60u) == 0u)
    {
        rt_kprintf("[gba] frame %u pc=%08x vblank=%u\n",
                   (unsigned int)g_frames, g_cpu.R[15], vbl);
    }

    gba_lcd_present(g_ppu->latched_framebuffer(),
                    g_ppu->render_width(), g_ppu->render_height());
    return false;
}

}  // namespace

extern "C" void gba_device_run(void *arena, unsigned int arena_bytes)
{
    const uintptr_t base = (reinterpret_cast<uintptr_t>(arena) + 15u) & ~uintptr_t(15u);
    const std::size_t need = sizeof(gba::GbaBus) + sizeof(gba::GbaPpu);

    rt_kprintf("[gba] GbaBus %u B, GbaPpu %u B, arena %u B\n",
               (unsigned int)sizeof(gba::GbaBus), (unsigned int)sizeof(gba::GbaPpu),
               arena_bytes);
    if (arena_bytes < need + 15u)
    {
        rt_kprintf("[gba] arena too small, need %u B\n", (unsigned int)(need + 15u));
        return;
    }

    g_bus = new (reinterpret_cast<void *>(base)) gba::GbaBus();
    g_ppu = new (reinterpret_cast<void *>(base + sizeof(gba::GbaBus))) gba::GbaPpu();
    print_heap("after machine init");
    run_static_initializers();

    std::string err;
    if (!g_bios.load_from_bytes(g_gba_bios_blob, g_gba_bios_blob_size,
                                gba::GbaBios::kExpectedSha1, &err))
    {
        rt_kprintf("[gba] BIOS rejected: %s\n", err.c_str());
        return;
    }
    print_heap("after BIOS load");

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
    print_heap("after cart load");

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

    rt_kprintf("[gba] BIOS loaded, booting from reset vector %08x\n", g_cpu.R[15]);
    debug_status_start();

    for (;;)
    {
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
            ++g_idle_pumps;
        }
        else
        {
            const uint32_t pc = g_cpu.R[15];
            if ((cart == nullptr) && (pc >= kCartBase))
            {
                break;
            }
            runtime_dispatch(pc);
            ++g_dispatch_returns;
        }

        // Slim codegen strips runtime_should_yield() from the cartridge, so the
        // guest-side frame hook never fires for cart code: present from here.
        present_frame();
    }

    rt_kprintf("[gba] BIOS left the ROM: pc=%08x, %u BIOS entries traced, %u frames\n",
               g_cpu.R[15], g_bios_entries, g_frames);
    rt_kprintf("[gba] halted (no cartridge mapped)\n");
}
