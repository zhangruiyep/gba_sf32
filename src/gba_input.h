/*
 * Board input for the GBA runtime.
 *
 * Phase 3.1 step 1: touch panel / board key bring-up only. The module owns the
 * "touch" device, samples it from its own thread and logs what it sees; no GBA
 * key is mapped yet.
 */

#ifndef __GBA_INPUT_H__
#define __GBA_INPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open the touch device and start the input sampling thread.
 * Safe to call once the LCD is up; does nothing but log a warning when the
 * board has no touch device.
 */
void gba_input_init(void);

#ifdef __cplusplus
}
#endif

#endif  // __GBA_INPUT_H__
