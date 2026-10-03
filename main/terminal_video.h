#pragma once
#include "moto/sdk.h"
bool terminal_video_init(void);
/* Local USB request: start/reuse the manual AP; credentials reply only over USB. */
bool terminal_video_manual_ap(void);
/* Save manual AP credentials in NVS and restore this AP automatically after reboot. */
bool terminal_video_configure_ap(const char *ssid, const char *password);
void terminal_video_event(const moto_event *event);
void terminal_video_poll(moto_sdk *sdk);
/* Called by UI task: copy only the latest complete RGB565 frame. */
bool terminal_video_copy(uint16_t *pixels, uint32_t *stream, uint64_t *frame, unsigned *generation);
/* Repaint notifications on a static video without claiming another received frame. */
bool terminal_video_copy_current(uint16_t *pixels);
void terminal_video_presented(uint32_t stream, uint64_t frame, unsigned generation);
bool terminal_video_active(void);
bool terminal_video_visible(void);

/* Preview is an inset on the selected instrument, never a fullscreen page. */
bool terminal_video_preview(void);
/* only_new=false also copies the last complete frame after stop/disconnect.
 * A frozen frame returns stream=0: repaint only, never acknowledge as a new frame. */
bool terminal_video_copy_map(uint16_t *pixels, bool only_new, uint32_t *stream,
                             uint64_t *frame, unsigned *generation);
