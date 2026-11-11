// gba_device_stubs.cpp — device replacements for the host-only runtime hooks.
//
// The host build gets these from runtime_arm_default_aborts.cpp, which depends
// on <filesystem>, <unistd.h> and the dynamic overlay loader — none of which
// exist on the bare-metal target. Phase 2-C executes recompiled code only, so
// every miss is a hard error reported over the serial port instead of being
// self-healed.

#include <cstdint>

#include "runtime_arm.h"
#include "multiplayer_slice.h"

extern "C" void rt_kprintf(const char *fmt, ...);

namespace {

[[noreturn]] void halt(const char *what, std::uint32_t pc)
{
    rt_kprintf("[gba] FATAL %s at pc=%08x\n", what, (unsigned int)pc);
    for (;;)
    {
    }
}

}  // namespace

// The overlay loader compiles missing code at run time on the host; there is
// no compiler on the device, so nothing is ever overlaid.
extern "C" int overlay_try_dispatch(std::uint32_t pc, int thumb)
{
    (void)pc;
    (void)thumb;
    return 0;
}

extern "C" void runtime_dispatch_miss(std::uint32_t target_pc)
{
    halt("dispatch miss", target_pc);
}

extern "C" void runtime_unimplemented_op(const char *op_name, std::uint32_t pc)
{
    rt_kprintf("[gba] FATAL unimplemented op %s at pc=%08x\n", op_name,
               (unsigned int)pc);
    for (;;)
    {
    }
}

extern "C" void runtime_force_interp_step(void)
{
    halt("interpreter step requested", g_cpu.R[15]);
}

// The host runtime implements this by walking the interpreter; the device
// build has no interpreter, so reaching it means the recompiled BIOS missed
// a function it must have.
extern "C" int runtime_bridge_interpret(std::uint32_t entry_pc, bool entry_thumb,
                                        std::uint32_t forced_stop_pc,
                                        std::uint64_t max_instrs)
{
    (void)entry_thumb;
    (void)forced_stop_pc;
    (void)max_instrs;
    halt("runtime bridge interpret requested", entry_pc);
}

// Never set on the device: it is the host's "run everything in the
// interpreter" diagnostic switch.
extern "C" int g_force_interp = 0;

namespace armv4t
{

// Timing model for ARM7TDMI multiplies. The implementation lives in
// external/arm-recomp-core/.../arm_ir.cpp, which also drags in the IR
// codegen; the device build only needs this one leaf function.
std::uint32_t mul_wait_cycles(std::uint32_t rs_value, bool signed_variant,
                              std::uint32_t extra) noexcept
{
    std::uint32_t wait = extra;
    if (signed_variant)
    {
        // Early-terminate on leading all-0s OR all-1s.
        if (((rs_value & 0xFFFFFF00u) == 0xFFFFFF00u) || ((rs_value & 0xFFFFFF00u) == 0u))
        {
            wait += 1;
        }
        else if (((rs_value & 0xFFFF0000u) == 0xFFFF0000u) || ((rs_value & 0xFFFF0000u) == 0u))
        {
            wait += 2;
        }
        else if (((rs_value & 0xFF000000u) == 0xFF000000u) || ((rs_value & 0xFF000000u) == 0u))
        {
            wait += 3;
        }
        else
        {
            wait += 4;
        }
    }
    else
    {
        // Unsigned long multiply: early-terminate on leading all-0s only.
        if ((rs_value & 0xFFFFFF00u) == 0u)
        {
            wait += 1;
        }
        else if ((rs_value & 0xFFFF0000u) == 0u)
        {
            wait += 2;
        }
        else if ((rs_value & 0xFF000000u) == 0u)
        {
            wait += 3;
        }
        else
        {
            wait += 4;
        }
    }
    return wait;
}

}  // namespace armv4t

namespace gbarecomp
{

// Single-player only: the multiplayer link slice never has work to do.
bool multiplayer_slice_safe(const gba::GbaBus &bus, const ArmCpuState &cpu)
{
    (void)bus;
    (void)cpu;
    return false;
}

}  // namespace gbarecomp
