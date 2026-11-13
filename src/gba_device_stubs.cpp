// gba_device_stubs.cpp — device replacements for the host-only runtime hooks.
//
// The host build gets these from runtime_arm_default_aborts.cpp, which depends
// on <filesystem>, <unistd.h> and the dynamic overlay loader — none of which
// exist on the bare-metal target.
//
// Dispatch misses are healed here by the reference interpreter (Tier3). The
// static finder cannot see code that is only reachable through a function
// pointer table in ROM/RAM, a computed jump into the middle of a function, or
// RAM-resident code, so a miss is a normal event and must not kill the
// firmware. runtime_bridge_interpret below mirrors the host self-heal contract
// (stop address, SWI routing, IRQ self-delivery, call-return stack unwinding)
// minus the host-only pieces (overlay loader, persistence, trace ring).

#include <cstdint>

#include "runtime_arm.h"
#include "runtime_context.h"
#include "multiplayer_slice.h"

#include "cpu_state.h"
#include "arm_ir.h"
#include "interpreter.h"
#include "arm_decode.h"
#include "thumb_decode.h"
#include "arm_cpu_bridge.h"
#include "runtime_bus_bridge.h"
#include "gba_bus.h"

extern "C" void rt_kprintf(const char *fmt, ...);

// IRQ nesting counters owned by runtime_arm.cpp; not exported by its header.
extern "C" uint32_t g_irq_nest_depth;
extern "C" uint32_t g_irq_iret_depth;

namespace {

[[noreturn]] void halt(const char *what, std::uint32_t pc)
{
    rt_kprintf("[gba] FATAL %s at pc=%08x\n", what, (unsigned int)pc);
    for (;;)
    {
    }
}

// Ceiling for one bridged subtree. A runaway (interpreted loop that never
// reaches the stop address) must fail loudly instead of hanging the firmware.
constexpr std::uint64_t kBridgeIterationCap = 50000000ull;

// Distinct missed PCs seen this session, kept so each one is logged once. A
// hot miss repeats many times per frame; serial logging would dominate.
constexpr std::uint32_t kMissPcSlots = 64;
std::uint32_t g_miss_pcs[kMissPcSlots];
std::uint32_t g_miss_pc_count = 0;
std::uint32_t g_miss_total = 0;
std::uint64_t g_bridged_insns = 0;

void note_miss(std::uint32_t pc, bool thumb)
{
    ++g_miss_total;
    for (std::uint32_t i = 0; i < g_miss_pc_count; ++i)
    {
        if (g_miss_pcs[i] == pc)
        {
            return;
        }
    }
    if (g_miss_pc_count < kMissPcSlots)
    {
        g_miss_pcs[g_miss_pc_count++] = pc;
    }
    rt_kprintf("[gba] dispatch miss pc=%08x thumb=%u -> interpreter (total %u)\n",
               (unsigned int)pc, (unsigned int)(thumb ? 1u : 0u),
               (unsigned int)g_miss_total);
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

// A miss means the static finder never reached this PC: it is only referenced
// through a function-pointer table, reached by a computed jump into a function
// body, or lives in RAM. Interpret that subtree and return to the recompiled
// caller instead of halting.
extern "C" void runtime_dispatch_miss(std::uint32_t target_pc)
{
    const std::uint32_t entry_pc = target_pc & ~1u;
    const bool entry_thumb = (g_cpu.cpsr & CPSR_T_BIT) != 0u;
    note_miss(entry_pc, entry_thumb);
    runtime_bridge_interpret(entry_pc, entry_thumb, 0u, 0u);
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

// Device port of the host self-heal bridge (runtime_arm_default_aborts.cpp).
// Runs the reference interpreter from `entry_pc` until control returns into
// statically compiled code, then leaves g_cpu at the resume point so the
// existing dispatch path takes over. Returns 1 when control was handed back,
// 0 when `max_instrs` cut the bridge short.
extern "C" int runtime_bridge_interpret(std::uint32_t entry_pc, bool entry_thumb,
                                        std::uint32_t forced_stop_pc,
                                        std::uint64_t max_instrs)
{
    gba::GbaBus *bus = gbarecomp::active_bus();
    if (bus == nullptr)
    {
        halt("bridge interpret without an active bus", entry_pc);
    }

    const std::uint32_t entry_depth = runtime_call_stack_depth();
    const std::uint32_t entry_floor = runtime_call_stack_floor();
    const bool top_level = (entry_depth == 0u);
    std::uint32_t stop_pc;
    if (forced_stop_pc != 0u)
    {
        stop_pc = forced_stop_pc & ~1u;
    }
    else if (!top_level)
    {
        // The compiled caller pushed its return address before dispatching.
        stop_pc = runtime_call_stack_data()[entry_depth - 1u] & ~1u;
    }
    else
    {
        // No pending frame: LR is only a fallback stop; returning into
        // compiled code (below) is what really ends a top-level bridge.
        stop_pc = g_cpu.R[14] & ~1u;
    }
    const bool irq_cont = (g_irq_nest_depth != 0u) && (forced_stop_pc == 0u);
    const std::uint64_t cap = (max_instrs != 0u) ? max_instrs : kBridgeIterationCap;

    armv4t::CPUState cpu{};
    gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);
    cpu.R[15] = entry_pc;
    cpu.cpsr.t = entry_thumb;
    cpu.thumb = entry_thumb;

    std::uint32_t unwound_depth = 0xFFFFFFFFu;
    std::uint64_t iters = 0;
    std::uint64_t bridged = 0;

    for (;;)
    {
        gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
        const std::uint32_t pc = cpu.R[15];
        const bool was_thumb = cpu.thumb;
        const std::uint32_t old_mode = cpu.cpsr.mode;
        bus->set_bios_access_enabled(pc < 0x00004000u);

        armv4t::Instr insn{};
        if (cpu.thumb)
        {
            insn = armv4t::ThumbDecoder::decode(bus->read16(pc), pc);
        }
        else
        {
            insn = armv4t::ArmDecoder::decode(bus->read32(pc), pc);
        }

        std::uint32_t cycles = 1;
        const armv4t::Interpreter::Result result =
            armv4t::Interpreter::step(cpu, *bus, insn, &cycles);
        ++bridged;

        if (result == armv4t::Interpreter::Result::NotImplemented ||
            result == armv4t::Interpreter::Result::Undefined)
        {
            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
            rt_kprintf("[gba] FATAL bridge %s at pc=%08x (miss pc=%08x)\n",
                       result == armv4t::Interpreter::Result::Undefined
                           ? "undefined instruction"
                           : "interpreter gap",
                       (unsigned int)pc, (unsigned int)entry_pc);
            for (;;)
            {
            }
        }

        if (result == armv4t::Interpreter::Result::Swi)
        {
            // Same contract as generated SWI code: point PC past the SWI, then
            // let runtime_swi enter the recompiled BIOS (it ticks the SWI).
            const std::uint32_t next_pc = pc + (cpu.thumb ? 2u : 4u);
            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
            g_cpu.R[15] = next_pc;
            runtime_swi(insn.swi_imm);
            gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);
        }
        else
        {
            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
            if (result == armv4t::Interpreter::Result::ExceptionReturn)
            {
                runtime_note_interpreted_exception_return(old_mode);
            }
            // Delivers pending IRQs through the recompiled BIOS, exactly as a
            // generated instruction's trailing runtime_tick would.
            runtime_tick(cycles);
            gbarecomp::load_arm_cpu_into_interp(g_cpu, cpu);
        }

        if (!irq_cont && (cpu.R[15] & ~1u) == stop_pc)
        {
            break;
        }

        // A shared epilogue can return past the immediate caller. Trim the
        // call-return stack to the matched outer frame, like the host bridge.
        if (!irq_cont && !top_level && forced_stop_pc == 0u && entry_depth >= 2u)
        {
            const std::uint32_t new_pc = cpu.R[15] & ~1u;
            const std::uint32_t seq_pc = (pc + (was_thumb ? 2u : 4u)) & ~1u;
            const bool is_call = (insn.op == armv4t::IrOp::BL) ||
                                 (insn.op == armv4t::IrOp::BL_suffix) ||
                                 (insn.op == armv4t::IrOp::BLX_reg);
            if ((new_pc != seq_pc) && !is_call)
            {
                const std::uint32_t *stack = runtime_call_stack_data();
                for (std::uint32_t slot = entry_depth - 1u; slot-- > entry_floor;)
                {
                    if ((stack[slot] & ~1u) == new_pc)
                    {
                        unwound_depth = slot;
                        break;
                    }
                }
                if (unwound_depth != 0xFFFFFFFFu)
                {
                    break;
                }
            }
        }

        if (irq_cont && (result == armv4t::Interpreter::Result::ExceptionReturn) &&
            (old_mode == 0x12u) && (g_irq_iret_depth == g_irq_nest_depth) &&
            !gbarecomp::runtime_has_resumable_context())
        {
            break;
        }

        if ((irq_cont || top_level) && ((cpu.R[15] & ~1u) != entry_pc) &&
            (runtime_has_static_entry(cpu.R[15], cpu.thumb ? 1u : 0u) != 0))
        {
            break;
        }

        if (++iters > cap)
        {
            gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
            if (max_instrs != 0u)
            {
                return 0;
            }
            rt_kprintf("[gba] FATAL bridge runaway entry=%08x stop=%08x pc=%08x insns=%u\n",
                       (unsigned int)entry_pc, (unsigned int)stop_pc,
                       (unsigned int)cpu.R[15], (unsigned int)bridged);
            for (;;)
            {
            }
        }
    }

    gbarecomp::store_interp_into_arm_cpu(cpu, g_cpu);
    bus->set_bios_access_enabled(g_cpu.R[15] < 0x00004000u);
    if (unwound_depth != 0xFFFFFFFFu)
    {
        runtime_call_stack_restore(runtime_call_stack_data(), unwound_depth);
    }
    else if (!top_level && !irq_cont)
    {
        // The bridged subroutine returned: pop the caller frame it consumed.
        runtime_call_stack_restore(runtime_call_stack_data(), entry_depth - 1u);
    }
    g_bridged_insns += bridged;
    return 1;
}

// Never set on the device: it is the host's "run everything in the
// interpreter" diagnostic switch.
extern "C" int g_force_interp = 0;

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
