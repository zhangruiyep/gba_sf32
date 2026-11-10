/*
 * GBA on SF32LB52 — display backend bring-up.
 *
 * Validates the rendering path before the GBA runtime is ported:
 *   - the panel is driven through the SDK framebuffer driver (drv_lcd_fb)
 *   - a 240x160 RGB565 GBA frame is shown centred on the panel, with
 *     black borders around it and no scaling
 *
 * The GBA frame buffer lives in on-chip SRAM (L1), the full-panel
 * buffer lives in OPI-PSRAM (L2) and is cleared to black once, so the
 * borders stay black while only the 240x160 window is refreshed.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#include "mem_section.h"

#ifdef BSP_USING_LCD_FRAMEBUFFER

#include "drv_lcd_fb.h"

#define GBA_WIDTH        240
#define GBA_HEIGHT       160
#define GBA_PIXEL_BYTES  2

#define PANEL_WIDTH      LCD_HOR_RES_MAX
#define PANEL_HEIGHT     LCD_VER_RES_MAX
#define GBA_OFFSET_X     ((PANEL_WIDTH - GBA_WIDTH) / 2)
#define GBA_OFFSET_Y     ((PANEL_HEIGHT - GBA_HEIGHT) / 2)

/* GBA frame, written by the (future) PPU. On-chip SRAM. */
L1_NON_RET_BSS_SECT_BEGIN(gba_fb)
L1_NON_RET_BSS_SECT(gba_fb, ALIGN(64) static uint8_t g_gba_fb[GBA_WIDTH * GBA_HEIGHT * GBA_PIXEL_BYTES]);
L1_NON_RET_BSS_SECT_END

/* Full panel buffer, only the 240x160 centre window is ever written. */
L2_NON_RET_BSS_SECT_BEGIN(lcd_fb)
L2_NON_RET_BSS_SECT(lcd_fb, ALIGN(64) static uint8_t g_lcd_fb[PANEL_WIDTH * PANEL_HEIGHT * GBA_PIXEL_BYTES]);
L2_NON_RET_BSS_SECT_END

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Moving colour-bar pattern, for eyeballing refresh and centring. */
static void render_test_pattern(uint32_t frame)
{
    uint16_t *fb = (uint16_t *)g_gba_fb;

    for (uint32_t y = 0; y < GBA_HEIGHT; y++)
    {
        for (uint32_t x = 0; x < GBA_WIDTH; x++)
        {
            uint32_t band = ((x + frame) / 30) % 6;
            static const uint16_t colors[6] = {
                0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800,
            };
            fb[y * GBA_WIDTH + x] = colors[band];
        }
    }

    /* Solid box in the middle, to confirm the window is not scaled. */
    for (uint32_t y = GBA_HEIGHT / 2 - 20; y < GBA_HEIGHT / 2 + 20; y++)
    {
        for (uint32_t x = GBA_WIDTH / 2 - 20; x < GBA_WIDTH / 2 + 20; x++)
        {
            fb[y * GBA_WIDTH + x] = rgb565(0, 0, 0);
        }
    }
}

static void display_backend_test(void)
{
    lcd_fb_desc_t fb_desc =
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

    LCD_AreaDef lcd_area =
    {
        .x0 = GBA_OFFSET_X,
        .y0 = GBA_OFFSET_Y,
        .x1 = GBA_OFFSET_X + GBA_WIDTH - 1,
        .y1 = GBA_OFFSET_Y + GBA_HEIGHT - 1,
    };

    LCD_AreaDef src_area =
    {
        .x0 = 0,
        .y0 = 0,
        .x1 = GBA_WIDTH - 1,
        .y1 = GBA_HEIGHT - 1,
    };

    RT_ASSERT(drv_lcd_fb_init("lcd") == RT_EOK);

    /* Clear the whole panel once; borders stay black after that. */
    memset(g_lcd_fb, 0, sizeof(g_lcd_fb));
    RT_ASSERT(drv_lcd_fb_set(&fb_desc) == RT_EOK);

    LCD_AreaDef full_area =
    {
        .x0 = 0,
        .y0 = 0,
        .x1 = PANEL_WIDTH - 1,
        .y1 = PANEL_HEIGHT - 1,
    };
    RT_ASSERT(drv_lcd_fb_write_send(&full_area, &full_area, g_lcd_fb, NULL, 1) == RT_EOK);
    drv_lcd_fb_wait_write_done(1000);

    rt_kprintf("panel %dx%d, GBA window at (%d,%d) %dx%d\n",
               PANEL_WIDTH, PANEL_HEIGHT,
               GBA_OFFSET_X, GBA_OFFSET_Y, GBA_WIDTH, GBA_HEIGHT);

    uint32_t frame = 0;
    uint32_t fps_start = rt_tick_get_millisecond();
    uint32_t fps_frames = 0;

    for (;;)
    {
        render_test_pattern(frame);

        RT_ASSERT(drv_lcd_fb_write_send(&lcd_area, &src_area, g_gba_fb, NULL, 1) == RT_EOK);

        frame++;
        fps_frames++;

        uint32_t now = rt_tick_get_millisecond();
        if (now - fps_start >= 1000)
        {
            rt_kprintf("display: %u fps\n", fps_frames * 1000u / (now - fps_start));
            fps_start = now;
            fps_frames = 0;
        }
    }
}

#endif /* BSP_USING_LCD_FRAMEBUFFER */

int main(void)
{
#ifdef BSP_USING_LCD_FRAMEBUFFER
    display_backend_test();
#endif

    return RT_EOK;
}
