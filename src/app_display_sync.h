// app_display_sync.h -- start an emulator in the bootloader's display mode.
//
// See app_display_sync.cpp. Does something only on boards with HSTX video,
// where the display mode (HDMI or DVI) exists; elsewhere it is an empty stub.
#ifndef APP_DISPLAY_SYNC_H
#define APP_DISPLAY_SYNC_H

#include <stdbool.h>

// C linkage: main.cpp includes this inside its extern "C" block.
#ifdef __cplusplus
extern "C" {
#endif

// Before the application with binary_info program name `prog_name` is flashed
// or started: when it is a pico_shared emulator, set the display mode in its
// settings file on the SD card to `dvi`. Every other application (the native
// ports, ColecoJam, unknown names) is left alone. Never fails the launch: a
// file it cannot read, understand or write is logged and skipped.
void appDisplaySync(const char *prog_name, bool dvi);

#ifdef __cplusplus
}
#endif

#endif // APP_DISPLAY_SYNC_H
