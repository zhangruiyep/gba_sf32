/*
 * Static virtual gamepad for the panel borders — Phase 3.1 step 2.
 *
 * The layout is expressed as offsets from the first panel line below the GBA
 * window, so the same numbers work on any panel height as long as the band is
 * at least GBA_PAD_BAND_MIN lines tall.
 *
 * Everything is painted with plain rect/circle/capsule fills into an RGB565
 * buffer: no font, no image assets, no allocation.
 */

#include <rtthread.h>

#include "gba_pad.h"

/* Classic GBA indigo shell with light grey caps. */
#define GBA_PAD_RGB565(r, g, b)     ((rt_uint16_t)((((r) & 0xF8) << 8) | \
                                                   (((g) & 0xFC) << 3) | \
                                                   ((b) >> 3)))

#define GBA_PAD_COLOR_BODY          GBA_PAD_RGB565(74, 62, 132)     // indigo shell
#define GBA_PAD_COLOR_BUTTON        GBA_PAD_RGB565(196, 196, 204)   // grey button cap
#define GBA_PAD_COLOR_EDGE          GBA_PAD_RGB565(104, 100, 136)   // darker outline

/* One bit per control, numbered like the guest KEYINPUT register. */
#define GBA_PAD_KEY_A               0x0001u
#define GBA_PAD_KEY_B               0x0002u
#define GBA_PAD_KEY_SELECT          0x0004u
#define GBA_PAD_KEY_START           0x0008u
#define GBA_PAD_KEY_RIGHT           0x0010u
#define GBA_PAD_KEY_LEFT            0x0020u
#define GBA_PAD_KEY_UP              0x0040u
#define GBA_PAD_KEY_DOWN            0x0080u
#define GBA_PAD_KEY_R               0x0100u
#define GBA_PAD_KEY_L               0x0200u

/*
 * Smallest band that still fits the d-pad below the screen, top gap included.
 * The bottom edge of the B cap sits at GBA_PAD_BAND_TOP_GAP + GBA_PAD_B_CY_OFF
 * + GBA_PAD_AB_R lines below the band top.
 */
#define GBA_PAD_BAND_MIN            160

/* Top margin inside the band, so the controls are centred in it. */
#define GBA_PAD_BAND_TOP_GAP        25

/* D-pad: a cross of two bars centred in the bottom band. */
#define GBA_PAD_DPAD_CX             72
#define GBA_PAD_DPAD_CY_OFF         72      // from the band top
#define GBA_PAD_DPAD_ARM_HALF       19
#define GBA_PAD_DPAD_REACH          61

/* A/B: two circles, B lower-left of A like on the real handheld. */
#define GBA_PAD_A_CX                330
#define GBA_PAD_A_CY_OFF            53
#define GBA_PAD_B_CX                262
#define GBA_PAD_B_CY_OFF            103
#define GBA_PAD_AB_R                30
#define GBA_PAD_AB_EDGE             3

/*
 * Select/Start: two small round caps stacked in a column right of the d-pad,
 * centred on the d-pad, the way the real shell tucks them between the d-pad
 * and the face buttons.
 */
#define GBA_PAD_ST_CX               178
#define GBA_PAD_SELECT_CY_OFF       48
#define GBA_PAD_START_CY_OFF        96
#define GBA_PAD_ST_R                16
#define GBA_PAD_ST_EDGE             2

/* L/R: one capsule per top corner. */
#define GBA_PAD_LR_Y0               28
#define GBA_PAD_L_X0                18
#define GBA_PAD_R_X0                294
#define GBA_PAD_LR_W                78
#define GBA_PAD_LR_H                38

typedef struct
{
	rt_uint16_t *fb;
	rt_uint32_t stride;     // pixels per row
	rt_int32_t  max_x;      // last drawable column, inclusive
	rt_int32_t  max_y;      // last drawable row, inclusive
} GBA_PAD_CANVAS_T;

static GBA_PAD_CANVAS_T g_pad_canvas = {RT_NULL, 0, 0, 0};

/*
 * Layout actually used by the last gba_pad_draw(). The hit test reads the same
 * origin, so the touch regions can never drift from the painted artwork.
 */
static rt_int32_t g_pad_band_y0 = 0;
static rt_bool_t  g_pad_controls_ready = RT_FALSE;  // bottom band was big enough
static rt_bool_t  g_pad_shoulder_ready = RT_FALSE;  // L/R were painted

/* Clamp a rectangle to the canvas, then fill it. Empty rects are dropped. */
static void pad_fill_rect(rt_int32_t x0, rt_int32_t y0, rt_int32_t x1, rt_int32_t y1,
                          rt_uint16_t color)
{
	rt_int32_t x = 0;
	rt_int32_t y = 0;

	if ((x1 < x0) || (y1 < y0))
	{
		return;
	}

	if (x0 < 0)
	{
		x0 = 0;
	}
	if (y0 < 0)
	{
		y0 = 0;
	}
	if (x1 > g_pad_canvas.max_x)
	{
		x1 = g_pad_canvas.max_x;
	}
	if (y1 > g_pad_canvas.max_y)
	{
		y1 = g_pad_canvas.max_y;
	}

	for (y = y0; y <= y1; y++)
	{
		rt_uint16_t *row = g_pad_canvas.fb + (rt_uint32_t)y * g_pad_canvas.stride;

		for (x = x0; x <= x1; x++)
		{
			row[x] = color;
		}
	}
}

/* Filled disc; the per-pixel bounds check also clips it to the canvas. */
static void pad_fill_circle(rt_int32_t cx, rt_int32_t cy, rt_int32_t r, rt_uint16_t color)
{
	rt_int32_t dy = 0;

	for (dy = -r; dy <= r; dy++)
	{
		rt_int32_t y = cy + dy;
		rt_int32_t dx = 0;
		rt_uint16_t *row = RT_NULL;

		if ((y < 0) || (y > g_pad_canvas.max_y))
		{
			continue;
		}

		row = g_pad_canvas.fb + (rt_uint32_t)y * g_pad_canvas.stride;
		for (dx = -r; dx <= r; dx++)
		{
			rt_int32_t x = cx + dx;

			if (((dx * dx) + (dy * dy)) > (r * r))
			{
				continue;
			}
			if ((x < 0) || (x > g_pad_canvas.max_x))
			{
				continue;
			}

			row[x] = color;
		}
	}
}

/* Capsule: a rect with the two vertical ends rounded off. */
static void pad_fill_pill(rt_int32_t x0, rt_int32_t y0, rt_int32_t x1, rt_int32_t y1,
                          rt_uint16_t color)
{
	rt_int32_t r = 0;

	if ((x1 < x0) || (y1 < y0))
	{
		return;
	}

	r = (y1 - y0) / 2;
	if (r <= 0)
	{
		pad_fill_rect(x0, y0, x1, y1, color);
		return;
	}
	if ((x1 - x0) < (2 * r))
	{
		r = (x1 - x0) / 2;
	}

	pad_fill_rect(x0 + r, y0, x1 - r, y1, color);
	pad_fill_circle(x0 + r, y0 + r, r, color);
	pad_fill_circle(x1 - r, y0 + r, r, color);
}

/* A cross of two overlapping bars. */
static void pad_draw_dpad(rt_int32_t cx, rt_int32_t cy)
{
	pad_fill_rect(cx - GBA_PAD_DPAD_ARM_HALF, cy - GBA_PAD_DPAD_REACH,
	              cx + GBA_PAD_DPAD_ARM_HALF, cy + GBA_PAD_DPAD_REACH,
	              GBA_PAD_COLOR_BUTTON);
	pad_fill_rect(cx - GBA_PAD_DPAD_REACH, cy - GBA_PAD_DPAD_ARM_HALF,
	              cx + GBA_PAD_DPAD_REACH, cy + GBA_PAD_DPAD_ARM_HALF,
	              GBA_PAD_COLOR_BUTTON);
}

/* Darker disc first, lighter disc on top: leaves a thin rim. */
static void pad_draw_round_cap(rt_int32_t cx, rt_int32_t cy, rt_int32_t r, rt_int32_t edge)
{
	pad_fill_circle(cx, cy, r, GBA_PAD_COLOR_EDGE);
	pad_fill_circle(cx, cy, r - edge, GBA_PAD_COLOR_BUTTON);
}

/* Point-in-disc, border included. */
static rt_bool_t pad_hit_circle(rt_int32_t x, rt_int32_t y, rt_int32_t cx, rt_int32_t cy,
                                rt_int32_t r)
{
	rt_int32_t dx = x - cx;
	rt_int32_t dy = y - cy;

	return (((dx * dx) + (dy * dy)) <= (r * r)) ? RT_TRUE : RT_FALSE;
}

/* Point-in-capsule: the rect minus the two rounded ends. */
static rt_bool_t pad_hit_pill(rt_int32_t x, rt_int32_t y, rt_int32_t x0, rt_int32_t y0,
                              rt_int32_t x1, rt_int32_t y1)
{
	rt_int32_t r = (y1 - y0) / 2;
	rt_int32_t cy = y0 + r;

	if ((x < x0) || (x > x1) || (y < y0) || (y > y1))
	{
		return RT_FALSE;
	}
	if (x < (x0 + r))
	{
		return pad_hit_circle(x, y, x0 + r, cy, r);
	}
	if (x > (x1 - r))
	{
		return pad_hit_circle(x, y, x1 - r, cy, r);
	}

	return RT_TRUE;
}

/*
 * Cross-shaped d-pad. The touch box is the square around the cross and each
 * axis fires once the finger leaves the central square, so the pivot presses
 * nothing while the corners still give clean diagonals; a touch panel has no
 * physical thumb to centre, so the forgiving square box plays better than the
 * exact painted outline.
 */
static rt_uint16_t pad_hit_dpad(rt_int32_t x, rt_int32_t y, rt_int32_t cx, rt_int32_t cy)
{
	rt_int32_t dx = x - cx;
	rt_int32_t dy = y - cy;
	rt_uint16_t mask = 0;

	if ((dx < -GBA_PAD_DPAD_REACH) || (dx > GBA_PAD_DPAD_REACH)
	    || (dy < -GBA_PAD_DPAD_REACH) || (dy > GBA_PAD_DPAD_REACH))
	{
		return 0;
	}

	if (dx > GBA_PAD_DPAD_ARM_HALF)
	{
		mask |= GBA_PAD_KEY_RIGHT;
	}
	if (dx < -GBA_PAD_DPAD_ARM_HALF)
	{
		mask |= GBA_PAD_KEY_LEFT;
	}
	if (dy > GBA_PAD_DPAD_ARM_HALF)
	{
		mask |= GBA_PAD_KEY_DOWN;
	}
	if (dy < -GBA_PAD_DPAD_ARM_HALF)
	{
		mask |= GBA_PAD_KEY_UP;
	}

	return mask;
}

rt_uint16_t gba_pad_hit(rt_uint16_t x, rt_uint16_t y)
{
	rt_int32_t px = (rt_int32_t)x;
	rt_int32_t py = (rt_int32_t)y;
	rt_uint16_t mask = 0;

	if (g_pad_controls_ready != RT_TRUE)
	{
		return 0;
	}

	mask |= pad_hit_dpad(px, py, GBA_PAD_DPAD_CX,
	                     g_pad_band_y0 + GBA_PAD_DPAD_CY_OFF);

	if (pad_hit_circle(px, py, GBA_PAD_A_CX, g_pad_band_y0 + GBA_PAD_A_CY_OFF,
	                   GBA_PAD_AB_R))
	{
		mask |= GBA_PAD_KEY_A;
	}
	if (pad_hit_circle(px, py, GBA_PAD_B_CX, g_pad_band_y0 + GBA_PAD_B_CY_OFF,
	                   GBA_PAD_AB_R))
	{
		mask |= GBA_PAD_KEY_B;
	}
	if (pad_hit_circle(px, py, GBA_PAD_ST_CX, g_pad_band_y0 + GBA_PAD_SELECT_CY_OFF,
	                   GBA_PAD_ST_R))
	{
		mask |= GBA_PAD_KEY_SELECT;
	}
	if (pad_hit_circle(px, py, GBA_PAD_ST_CX, g_pad_band_y0 + GBA_PAD_START_CY_OFF,
	                   GBA_PAD_ST_R))
	{
		mask |= GBA_PAD_KEY_START;
	}

	if (g_pad_shoulder_ready == RT_TRUE)
	{
		if (pad_hit_pill(px, py, GBA_PAD_L_X0, GBA_PAD_LR_Y0,
		                 GBA_PAD_L_X0 + GBA_PAD_LR_W - 1, GBA_PAD_LR_Y0 + GBA_PAD_LR_H - 1))
		{
			mask |= GBA_PAD_KEY_L;
		}
		if (pad_hit_pill(px, py, GBA_PAD_R_X0, GBA_PAD_LR_Y0,
		                 GBA_PAD_R_X0 + GBA_PAD_LR_W - 1, GBA_PAD_LR_Y0 + GBA_PAD_LR_H - 1))
		{
			mask |= GBA_PAD_KEY_R;
		}
	}

	return mask;
}

void gba_pad_draw(rt_uint16_t *fb, rt_uint32_t panel_w, rt_uint32_t panel_h,
                  rt_uint32_t content_y0, rt_uint32_t content_y1)
{
	rt_int32_t band_y0 = 0;

	if ((fb == RT_NULL) || (panel_w == 0) || (panel_h == 0) || (content_y1 >= panel_h))
	{
		return;
	}

	g_pad_canvas.fb = fb;
	g_pad_canvas.stride = panel_w;
	g_pad_canvas.max_x = (rt_int32_t)panel_w - 1;
	g_pad_canvas.max_y = (rt_int32_t)panel_h - 1;
	g_pad_controls_ready = RT_FALSE;
	g_pad_shoulder_ready = RT_FALSE;

	/* The whole panel is the indigo shell; the screen window paints over it. */
	pad_fill_rect(0, 0, g_pad_canvas.max_x, g_pad_canvas.max_y, GBA_PAD_COLOR_BODY);

	/* Shoulder buttons, only when the band above the screen is tall enough. */
	if ((rt_int32_t)content_y0 >= (GBA_PAD_LR_Y0 + GBA_PAD_LR_H))
	{
		pad_fill_pill(GBA_PAD_L_X0, GBA_PAD_LR_Y0,
		              GBA_PAD_L_X0 + GBA_PAD_LR_W - 1, GBA_PAD_LR_Y0 + GBA_PAD_LR_H - 1,
		              GBA_PAD_COLOR_BUTTON);
		pad_fill_pill(GBA_PAD_R_X0, GBA_PAD_LR_Y0,
		              GBA_PAD_R_X0 + GBA_PAD_LR_W - 1, GBA_PAD_LR_Y0 + GBA_PAD_LR_H - 1,
		              GBA_PAD_COLOR_BUTTON);
		g_pad_shoulder_ready = RT_TRUE;
	}

	band_y0 = (rt_int32_t)content_y1 + 1;
	if ((g_pad_canvas.max_y - band_y0) < GBA_PAD_BAND_MIN)
	{
		return;     // no room below the screen, keep the plain shell
	}
	band_y0 += GBA_PAD_BAND_TOP_GAP;

	pad_draw_dpad(GBA_PAD_DPAD_CX, band_y0 + GBA_PAD_DPAD_CY_OFF);
	pad_draw_round_cap(GBA_PAD_A_CX, band_y0 + GBA_PAD_A_CY_OFF,
	                   GBA_PAD_AB_R, GBA_PAD_AB_EDGE);
	pad_draw_round_cap(GBA_PAD_B_CX, band_y0 + GBA_PAD_B_CY_OFF,
	                   GBA_PAD_AB_R, GBA_PAD_AB_EDGE);

	pad_draw_round_cap(GBA_PAD_ST_CX, band_y0 + GBA_PAD_SELECT_CY_OFF,
	                   GBA_PAD_ST_R, GBA_PAD_ST_EDGE);
	pad_draw_round_cap(GBA_PAD_ST_CX, band_y0 + GBA_PAD_START_CY_OFF,
	                   GBA_PAD_ST_R, GBA_PAD_ST_EDGE);

	/* Publish the origin the hit test works from. */
	g_pad_band_y0 = band_y0;
	g_pad_controls_ready = RT_TRUE;
}
