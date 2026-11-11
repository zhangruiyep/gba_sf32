/*
 * gba_cart_sd.c — load the cartridge image from the TF card into OPI-PSRAM.
 *
 * The recompiled cartridge code runs from NOR XIP, but it still reads the
 * original ROM bytes at run time (graphics, level data, audio tables), so the
 * whole image must stay resident. PSRAM is the only memory large enough.
 *
 * The card is read through a small on-chip SRAM bounce buffer instead of
 * reading straight into PSRAM: the SPI MSD driver hands the caller's buffer to
 * DMA, and PSRAM is cacheable, so a direct DMA target would leave the cache
 * holding stale lines.
 */

#include <rtthread.h>
#include <string.h>

#include <fcntl.h>

#include <dfs_file.h>
#include <dfs_fs.h>

#include "mem_section.h"

/* A GBA cartridge is at most 32 MiB, but the supported images here are 4 MiB
 * (Mario Kart SC). Keep the buffer at the largest image we recompile. */
#define GBA_CART_MAX_BYTES      (4u * 1024u * 1024u)
#define GBA_CART_BOUNCE_BYTES   (16u * 1024u)
#define GBA_CART_MOUNT_WAIT_MS  3000u

L2_NON_RET_BSS_SECT_BEGIN(gba_cart)
L2_NON_RET_BSS_SECT(gba_cart, ALIGN(64) static uint8_t g_gba_cart_image[GBA_CART_MAX_BYTES]);
L2_NON_RET_BSS_SECT_END

L1_NON_RET_BSS_SECT_BEGIN(gba_cart_io)
L1_NON_RET_BSS_SECT(gba_cart_io, ALIGN(32) static uint8_t g_gba_cart_bounce[GBA_CART_BOUNCE_BYTES]);
L1_NON_RET_BSS_SECT_END

static rt_bool_t s_mounted = RT_FALSE;

static rt_bool_t cart_mount(void)
{
    rt_uint32_t waited = 0;

    if (s_mounted)
    {
        return RT_TRUE;
    }

    /* spi_msd registers "sd0" from its INIT_PREV_EXPORT hook, which may run
     * after this thread starts. */
    while ((rt_device_find("sd0") == RT_NULL) && (waited < GBA_CART_MOUNT_WAIT_MS))
    {
        rt_thread_mdelay(50);
        waited += 50;
    }

    if (rt_device_find("sd0") == RT_NULL)
    {
        rt_kprintf("[gba] TF card: sd0 not registered\n");
        return RT_FALSE;
    }

    if (dfs_mount("sd0", "/", "elm", 0, 0) != 0)
    {
        rt_kprintf("[gba] TF card: mount failed\n");
        return RT_FALSE;
    }

    s_mounted = RT_TRUE;
    rt_kprintf("[gba] TF card mounted on /\n");
    return RT_TRUE;
}

/* Returns the number of bytes copied into the PSRAM image, 0 on failure. */
size_t gba_cart_load(const char *path)
{
    struct dfs_fd fd;
    size_t total = 0;

    if (!cart_mount())
    {
        return 0;
    }

    memset(&fd, 0, sizeof(fd));
    if (dfs_file_open(&fd, path, O_RDONLY) != 0)
    {
        rt_kprintf("[gba] %s not found\n", path);
        return 0;
    }

    if ((fd.size == 0) || (fd.size > sizeof(g_gba_cart_image)))
    {
        rt_kprintf("[gba] %s is %u B, expected 1..%u B\n", path,
                   (unsigned int)fd.size, (unsigned int)sizeof(g_gba_cart_image));
        dfs_file_close(&fd);
        return 0;
    }

    for (;;)
    {
        int n = dfs_file_read(&fd, g_gba_cart_bounce, sizeof(g_gba_cart_bounce));

        if (n < 0)
        {
            rt_kprintf("[gba] read %s failed at offset %u\n", path, (unsigned int)total);
            total = 0;
            break;
        }
        if (n == 0)
        {
            break;
        }
        if (total + (size_t)n > sizeof(g_gba_cart_image))
        {
            rt_kprintf("[gba] %s exceeds %u B\n", path,
                       (unsigned int)sizeof(g_gba_cart_image));
            total = 0;
            break;
        }

        memcpy(&g_gba_cart_image[total], g_gba_cart_bounce, (size_t)n);
        total += (size_t)n;
    }

    dfs_file_close(&fd);
    return total;
}

const uint8_t *gba_cart_image(void)
{
    return g_gba_cart_image;
}

size_t gba_cart_capacity(void)
{
    return sizeof(g_gba_cart_image);
}
