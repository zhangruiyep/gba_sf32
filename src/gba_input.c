/*
 * Board input for the GBA runtime — Phase 3.1.
 *
 * Step 1 proved the FT6146 touch panel and the two board keys are reachable.
 * Step 2 painted the virtual gamepad into the panel borders (src/gba_pad.c).
 * This step closes the loop: every touch report is hit-tested against that
 * artwork and the resulting button mask is written into the guest KEYINPUT
 * register, so the painted pad actually plays the game.
 *
 * The touch driver (drv_touch.c) samples the panel from its own thread and
 * pushes records into a ring buffer, so draining that ring here at a slower
 * rate only adds latency; it never lowers the panel's sampling rate.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "drv_touch.h"
#include "gba_input.h"
#include "gba_pad.h"

#define GBA_IN_TOUCH_NAME       "touch"     // drv_touch.c registers this name
#define GBA_IN_POLL_MS          10          // ring drain period in milliseconds
#define GBA_IN_DRAIN_MAX        16          // records drained per poll
#define GBA_IN_GRID             5           // orientation grid, matches the corners
#define GBA_IN_THREAD_STACK     2048
#define GBA_IN_THREAD_PRIO      RT_THREAD_PRIORITY_HIGH
#define GBA_IN_THREAD_TICK      RT_TICK_PER_SECOND

/* KEYINPUT only defines the ten button bits; the upper bits read as zero. */
#define GBA_IN_KEY_MASK         0x03FFu

/*
 * Implemented in gba_device.cpp: writes the active-low KEYINPUT value into the
 * emulated machine. Only this module needs it, so it is declared here rather
 * than in a shared header.
 */
extern void gba_device_set_keyinput(rt_uint16_t keys);

static rt_device_t g_touch_dev = RT_NULL;
static rt_thread_t g_input_thread = RT_NULL;
static rt_uint16_t g_keys_pressed = 0;      // active-high, guest bit order

/*
 * Log one touch record in panel coordinates plus the coarse grid cell it falls
 * into, so the four corners can be checked against the panel orientation with
 * a single finger tap.
 */
static void gba_input_log_touch(const struct touch_message *msg)
{
	rt_uint32_t cell_x = (rt_uint32_t)msg->x * GBA_IN_GRID / LCD_HOR_RES_MAX;
	rt_uint32_t cell_y = (rt_uint32_t)msg->y * GBA_IN_GRID / LCD_VER_RES_MAX;

	rt_kprintf("[gba] tp ev=%u x=%u y=%u cell=%u,%u\n",
	           (unsigned int)msg->event, (unsigned int)msg->x,
	           (unsigned int)msg->y, (unsigned int)cell_x,
	           (unsigned int)cell_y);
}

/* Log both board keys, but only when a level changes, to keep the log quiet. */
static void gba_input_log_keys(void)
{
	static rt_int32_t last_key1 = -1;   // -1 forces the first report
	static rt_int32_t last_key2 = -1;
	rt_int32_t key1 = 0;
	rt_int32_t key2 = 0;

#if defined(BSP_KEY1_PIN) && (BSP_KEY1_PIN >= 0)
	key1 = rt_pin_read(BSP_KEY1_PIN);
#if defined(BSP_KEY1_ACTIVE_HIGH) && (BSP_KEY1_ACTIVE_HIGH == 0)
	key1 = (key1 == 0) ? 1 : 0;
#endif
#endif

#if defined(BSP_KEY2_PIN) && (BSP_KEY2_PIN >= 0)
	key2 = rt_pin_read(BSP_KEY2_PIN);
#if defined(BSP_KEY2_ACTIVE_HIGH) && (BSP_KEY2_ACTIVE_HIGH == 0)
	key2 = (key2 == 0) ? 1 : 0;
#endif
#endif

	if ((key1 != last_key1) || (key2 != last_key2))
	{
		last_key1 = key1;
		last_key2 = key2;
		rt_kprintf("[gba] key1=%d key2=%d\n", (int)key1, (int)key2);
	}
}

/*
 * Turn one touch report into a KEYINPUT update. DOWN and MOVE re-run the hit
 * test, so a finger can slide from one control to the next; UP releases
 * everything. Any other event leaves the current state alone, and an unchanged
 * mask costs no register write.
 */
static void gba_input_apply_touch(const struct touch_message *msg)
{
	rt_uint16_t pressed = g_keys_pressed;

	if (msg->event == TOUCH_EVENT_UP)
	{
		pressed = 0;
	}
	else if ((msg->event == TOUCH_EVENT_DOWN) || (msg->event == TOUCH_EVENT_MOVE))
	{
		pressed = gba_pad_hit(msg->x, msg->y);
	}

	if (pressed == g_keys_pressed)
	{
		return;
	}

	g_keys_pressed = pressed;
	gba_device_set_keyinput((rt_uint16_t)(~pressed) & GBA_IN_KEY_MASK);
	/* Bit 0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down, 8 R, 9 L. */
	rt_kprintf("[gba] pressed=0x%03x keyinput=0x%03x\n",
	           (unsigned int)pressed,
	           (unsigned int)((~pressed) & GBA_IN_KEY_MASK));
}

static void gba_input_thread_entry(void *parameter)
{
	struct touch_message msg = {0};

	(void)parameter;

	for (;;)
	{
		if (g_touch_dev != RT_NULL)
		{
			for (rt_uint32_t i = 0; i < GBA_IN_DRAIN_MAX; i++)
			{
				if (rt_device_read(g_touch_dev, 0, &msg, 1) == 0)
				{
					break;      // ring empty, nothing new since last poll
				}

				gba_input_log_touch(&msg);
				gba_input_apply_touch(&msg);
			}
		}

		gba_input_log_keys();
		rt_thread_mdelay(GBA_IN_POLL_MS);
	}
}

/* Claim the touch device; a missing panel is not fatal for this step. */
static void gba_input_open_touch(void)
{
	rt_device_t dev = rt_device_find(GBA_IN_TOUCH_NAME);

	if (dev == RT_NULL)
	{
		rt_kprintf("[gba] input: no \"%s\" device\n", GBA_IN_TOUCH_NAME);
		return;
	}

	if (rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
	{
		rt_kprintf("[gba] input: touch open failed\n");
		return;
	}

	g_touch_dev = dev;
	rt_kprintf("[gba] input: touch ready (%dx%d)\n",
	           (int)LCD_HOR_RES_MAX, (int)LCD_VER_RES_MAX);
}

void gba_input_init(void)
{
	gba_input_open_touch();

#if defined(BSP_KEY1_PIN) && (BSP_KEY1_PIN >= 0)
	rt_pin_mode(BSP_KEY1_PIN, PIN_MODE_INPUT);
#endif
#if defined(BSP_KEY2_PIN) && (BSP_KEY2_PIN >= 0)
	rt_pin_mode(BSP_KEY2_PIN, PIN_MODE_INPUT);
#endif

	g_input_thread = rt_thread_create("gba_in", gba_input_thread_entry, RT_NULL,
	                                  GBA_IN_THREAD_STACK, GBA_IN_THREAD_PRIO,
	                                  GBA_IN_THREAD_TICK);
	if (g_input_thread == RT_NULL)
	{
		rt_kprintf("[gba] input: thread create failed\n");
		return;
	}

	rt_thread_startup(g_input_thread);
	rt_kprintf("[gba] input: touch drives the on-screen pad\n");
}
