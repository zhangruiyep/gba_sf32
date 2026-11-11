/*
 * GBA on SF32LB52 — device entry point.
 *
 * Phase 2-C: the recompiled GBA runtime runs the guest BIOS from its reset
 * vector, and each VBlank the latched 240x160 RGB888 frame is converted and
 * pushed to the panel through the SDK framebuffer driver (drv_lcd_fb).
 *
 * Memory placement:
 *   - the 240x160 RGB565 GBA window lives in on-chip SRAM (L1)
 *   - the full-panel buffer lives in OPI-PSRAM (L2) and is cleared to black
 *     once, so the borders stay black while only the GBA window is refreshed
 *   - the GBA machine (bus + PPU) is placement-new'ed in a PSRAM arena below,
 *     because it owns ~386 KB of guest RAM and two 115 KB framebuffers
 *
 * The GBA runtime itself stays C++ and free of RT-Thread headers; this file
 * owns the board-specific glue.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#include "mem_section.h"

#define GBA_WIDTH        240
#define GBA_HEIGHT       160
#define GBA_PIXEL_BYTES  2

#ifdef BSP_USING_LCD_FRAMEBUFFER

#include "drv_lcd_fb.h"

#define PANEL_WIDTH      LCD_HOR_RES_MAX
#define PANEL_HEIGHT     LCD_VER_RES_MAX
#define GBA_OFFSET_X     ((PANEL_WIDTH - GBA_WIDTH) / 2)
#define GBA_OFFSET_Y     ((PANEL_HEIGHT - GBA_HEIGHT) / 2)

/* GBA frame, converted from the PPU output. On-chip SRAM. */
L1_NON_RET_BSS_SECT_BEGIN(gba_fb)
L1_NON_RET_BSS_SECT(gba_fb, ALIGN(64) static uint8_t g_gba_fb[GBA_WIDTH * GBA_HEIGHT * GBA_PIXEL_BYTES]);
L1_NON_RET_BSS_SECT_END

/* Full panel buffer, used once to clear the borders. */
L2_NON_RET_BSS_SECT_BEGIN(lcd_fb)
L2_NON_RET_BSS_SECT(lcd_fb, ALIGN(64) static uint8_t g_lcd_fb[PANEL_WIDTH * PANEL_HEIGHT * GBA_PIXEL_BYTES]);
L2_NON_RET_BSS_SECT_END

/*
 * Two framebuffers are registered with drv_lcd_fb:
 *   - the whole panel, only to paint the borders black at boot;
 *   - the 240x160 GBA window, which is what gets flushed every frame.
 *
 * The driver always flushes fb_clip with the pixel count of fb.area, and the
 * LCDC places a layer at the layer's own top-left position, so a framebuffer
 * smaller than the panel is the only way to push just the GBA window: one
 * 76.8 KB transfer instead of a 351 KB full panel one.
 */
static lcd_fb_desc_t g_panel_fb_desc =
{
    .area.x0 = 0,
    .area.y0 = 0,
    .area.x1 = PANEL_WIDTH - 1,
    .area.y1 = PANEL_HEIGHT - 1,
    .p_data = (uint8_t *)g_lcd_fb,
    .cmpr_rate = 0,
    .line_bytes = PANEL_WIDTH * GBA_PIXEL_BYTES,
    .format = RTGRAPHIC_PIXEL_FORMAT_RGB565,
};

static lcd_fb_desc_t g_gba_fb_desc =
{
    .area.x0 = GBA_OFFSET_X,
    .area.y0 = GBA_OFFSET_Y,
    .area.x1 = GBA_OFFSET_X + GBA_WIDTH - 1,
    .area.y1 = GBA_OFFSET_Y + GBA_HEIGHT - 1,
    .p_data = (uint8_t *)g_gba_fb,
    .cmpr_rate = 0,
    .line_bytes = GBA_WIDTH * GBA_PIXEL_BYTES,
    .format = RTGRAPHIC_PIXEL_FORMAT_RGB565,
};

/* The GBA window in panel coordinates; both write and source area use it. */
static LCD_AreaDef g_gba_area =
{
    .x0 = GBA_OFFSET_X,
    .y0 = GBA_OFFSET_Y,
    .x1 = GBA_OFFSET_X + GBA_WIDTH - 1,
    .y1 = GBA_OFFSET_Y + GBA_HEIGHT - 1,
};

static void gba_lcd_init(void)
{
    if (drv_lcd_fb_init("lcd") != RT_EOK)
    {
        rt_kprintf("[gba] LCD init failed\n");
        return;
    }

    /* Clear the whole panel once; the borders stay black after that. */
    memset(g_lcd_fb, 0, sizeof(g_lcd_fb));
    RT_ASSERT(drv_lcd_fb_set(&g_panel_fb_desc) == RT_EOK);

    LCD_AreaDef full_area =
    {
        .x0 = 0,
        .y0 = 0,
        .x1 = PANEL_WIDTH - 1,
        .y1 = PANEL_HEIGHT - 1,
    };
    RT_ASSERT(drv_lcd_fb_write_send(&full_area, &full_area, g_lcd_fb, NULL, 1) == RT_EOK);
    while (drv_lcd_fb_is_busy())
    {
        rt_thread_mdelay(2);
    }

    /* Switch to the 240x160 window framebuffer for every later frame. */
    RT_ASSERT(drv_lcd_fb_set(&g_gba_fb_desc) == RT_EOK);

    rt_kprintf("[gba] panel %dx%d, GBA window at (%d,%d) %dx%d\n",
               PANEL_WIDTH, PANEL_HEIGHT,
               GBA_OFFSET_X, GBA_OFFSET_Y, GBA_WIDTH, GBA_HEIGHT);
}

/*
 * Present one PPU frame (RGB888, packed, no padding). Called from the GBA
 * runtime's VBlank hook.
 *
 * g_gba_fb is the registered framebuffer's own memory, so passing it back with
 * the matching area makes the driver skip the copy and just flush the window.
 * write_send(..., send = 1) already waits for the previous flush to finish, so
 * there is no separate wait here (drv_lcd_fb_wait_write_done() would consume the
 * write-done event and make the next call block until its 100 ms timeout).
 */
void gba_lcd_present(const uint8_t *rgb888, uint32_t width, uint32_t height)
{
    if ((rgb888 == NULL) || (width != GBA_WIDTH) || (height != GBA_HEIGHT))
    {
        return;     /* no scaler; wider PPU modes are not used on this build */
    }

    uint16_t *dst = (uint16_t *)g_gba_fb;
    for (uint32_t i = 0; i < GBA_WIDTH * GBA_HEIGHT; i++)
    {
        dst[i] = (uint16_t)(((rgb888[0] & 0xF8) << 8) |
                            ((rgb888[1] & 0xFC) << 3) |
                            (rgb888[2] >> 3));
        rgb888 += 3;
    }

    drv_lcd_fb_write_send(&g_gba_area, &g_gba_area, g_gba_fb, NULL, 1);
}

#else

void gba_lcd_present(const uint8_t *rgb888, uint32_t width, uint32_t height)
{
    (void)rgb888;
    (void)width;
    (void)height;
}

#endif /* BSP_USING_LCD_FRAMEBUFFER */

/* GBA machine arena: GbaBus + GbaPpu live here, in OPI-PSRAM. */
#define GBA_ARENA_BYTES  (1024 * 1024)

L2_NON_RET_BSS_SECT_BEGIN(gba_arena)
L2_NON_RET_BSS_SECT(gba_arena, ALIGN(64) static uint8_t g_gba_arena[GBA_ARENA_BYTES]);
L2_NON_RET_BSS_SECT_END

/* Implemented in gba_device.cpp: never returns while the guest is running. */
extern void gba_device_run(void *arena, unsigned int arena_bytes);

int main(void)
{
    rt_kprintf("[gba] SF32LB52 GBA runtime, Phase 2-C\n");

#ifdef BSP_USING_LCD_FRAMEBUFFER
    gba_lcd_init();
#endif

    gba_device_run(g_gba_arena, (unsigned int)sizeof(g_gba_arena));

    rt_kprintf("[gba] runtime returned\n");
    return RT_EOK;
}
