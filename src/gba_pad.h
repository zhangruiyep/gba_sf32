/*
 * Static virtual gamepad painted into the panel borders.
 *
 * Phase 3.1: the panel is 390x450 and the GBA picture only occupies the 240x160
 * window at (75,96), so there are free bands above and below it. This module
 * paints the whole panel in the classic GBA indigo and puts grey buttons in
 * those bands, so the device reads as a handheld instead of a black frame.
 */
#ifndef __GBA_PAD_H__
#define __GBA_PAD_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Paint the gamepad into an RGB565 panel buffer. The buffer row 0 is the top
 * panel line and the stride is panel_w pixels.
 *
 * fb          panel framebuffer
 * panel_w     panel width in pixels
 * panel_h     panel height in pixels
 * content_y0  first panel line owned by the emulated screen
 * content_y1  last panel line owned by the emulated screen
 */
void gba_pad_draw(rt_uint16_t *fb, rt_uint32_t panel_w, rt_uint32_t panel_h,
                  rt_uint32_t content_y0, rt_uint32_t content_y1);

/*
 * Hit-test a panel coordinate against the controls painted by gba_pad_draw().
 * The returned mask uses the guest KEYINPUT bit order (bit 0 A, 1 B, 2 Select,
 * 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L) with a set bit meaning the
 * button is held, so the caller only has to invert it for the active-low
 * register. Returns 0 before the pad has been drawn and for misses.
 */
rt_uint16_t gba_pad_hit(rt_uint16_t x, rt_uint16_t y);

#ifdef __cplusplus
}
#endif

#endif  // __GBA_PAD_H__
