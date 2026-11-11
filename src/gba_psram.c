/*
 * gba_psram.c — external PSRAM arena for the GBA runtime's large buffers.
 *
 * The on-chip SRAM heap is only ~320 KB, while some runtime buffers are
 * multi-megabyte and infrequently touched (the always-on audio capture ring,
 * and later the cartridge image). They are served from a static arena placed
 * in OPI-PSRAM through the L2 non-retained section, the same way InfoNES and
 * PureDOOM place their big emulator buffers.
 *
 * The arena is a bump allocator: allocations are made once during bring-up and
 * live for the whole session, so there is nothing to reclaim. Every block is
 * zeroed on allocation because PSRAM contents are undefined after power-on.
 */

#include <rtthread.h>
#include <string.h>

#include "mem_section.h"

#define GBA_PSRAM_ARENA_BYTES   (4 * 1024 * 1024)
#define GBA_PSRAM_ALIGN         64

L2_NON_RET_BSS_SECT_BEGIN(gba_psram)
L2_NON_RET_BSS_SECT(gba_psram, ALIGN(64) static uint8_t g_gba_psram_arena[GBA_PSRAM_ARENA_BYTES]);
L2_NON_RET_BSS_SECT_END

static size_t s_used = 0;

void *gba_psram_alloc(size_t size)
{
    void *ptr;
    size_t offset;
    size_t total;

    if (size == 0)
    {
        return RT_NULL;
    }

    total = (size + (GBA_PSRAM_ALIGN - 1)) & ~(size_t)(GBA_PSRAM_ALIGN - 1);

    rt_enter_critical();
    offset = s_used;
    if (total > sizeof(g_gba_psram_arena) - offset)
    {
        rt_exit_critical();
        rt_kprintf("[gba] PSRAM arena full: %u requested, %u free\n",
                   (unsigned int)total,
                   (unsigned int)(sizeof(g_gba_psram_arena) - offset));
        return RT_NULL;
    }
    s_used = offset + total;
    rt_exit_critical();

    ptr = &g_gba_psram_arena[offset];
    memset(ptr, 0, size);
    return ptr;
}

/* Nothing is reclaimed: the arena is released when the runtime stops. */
void gba_psram_free(void *ptr)
{
    (void)ptr;
}

size_t gba_psram_used(void)
{
    return s_used;
}

size_t gba_psram_capacity(void)
{
    return sizeof(g_gba_psram_arena);
}
