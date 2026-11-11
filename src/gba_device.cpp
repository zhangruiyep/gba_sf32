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

#include "runtime_arm.h"          // g_cpu, runtime_dispatch, g_runtime_fn_entry_hook
#include "runtime_bus_bridge.h"   // gbarecomp::set_active_bus / set_active_ppu
#include "gba_bios.h"
#include "gba_bus.h"
#include "gba_ppu.h"

extern "C" void rt_kprintf(const char *fmt, ...);

// RT-Thread's operator new returns nullptr instead of throwing, so an oversized
// std::vector inside the runtime shows up as a null-pointer fault with no hint.
// Print the heap high-water mark around each bring-up step.
extern "C" void rt_memory_info(unsigned long *total, unsigned long *used,
                               unsigned long *max_used);

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

void print_heap(const char *stage)
{
    unsigned long total = 0;
    unsigned long used = 0;
    unsigned long max_used = 0;

    rt_memory_info(&total, &used, &max_used);
    rt_kprintf("[gba] heap %s: total=%lu used=%lu max=%lu\n",
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

// Called at VBlank start with the guest stack left in place.
bool present_frame(void)
{
    if (g_ppu == nullptr)
    {
        return false;
    }

    if ((++g_frames % 60u) == 0u)
    {
        rt_kprintf("[gba] frame %u pc=%08x vblank=%llu\n",
                   g_frames, g_cpu.R[15], g_runtime_vblank_starts);
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

    g_bus->set_bios(&g_bios);
    g_bus->set_rom(cart, cart_size);
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

    for (;;)
    {
        const uint32_t pc = g_cpu.R[15];
        if ((cart == nullptr) && (pc >= kCartBase))
        {
            break;
        }
        runtime_dispatch(pc);
    }

    rt_kprintf("[gba] BIOS left the ROM: pc=%08x, %u BIOS entries traced, %u frames\n",
               g_cpu.R[15], g_bios_entries, g_frames);
    rt_kprintf("[gba] halted (no cartridge mapped)\n");
}
