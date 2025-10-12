#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#define CFG_TUSB_OS OPT_OS_FREERTOS

#define BOARD_TUH_RHPORT 0
#define BOARD_TUH_RHPORT_SPEED OPT_MODE_FULL_SPEED

#define CFG_TUH_ENABLED 1

#define CFG_TUH_HUB 1
#define CFG_TUH_MIDI 4
#define CFG_TUH_MIDI_RX_BUFSIZE 64
#define CFG_TUH_MIDI_TX_BUFSIZE 64

#define CFG_TUH_DEVICE_MAX 4
#define CFG_TUH_DWC2_DMA_ENABLE 1

#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUSB_DEBUG 1
#define CFG_TUSB_DEBUG_PRINTF printf

#ifdef __cplusplus
}
#endif
