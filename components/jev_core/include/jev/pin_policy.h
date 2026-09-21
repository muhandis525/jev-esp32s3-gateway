#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JEV_PIN_OK = 0,
    JEV_PIN_OUT_OF_RANGE,
    JEV_PIN_NOT_IMPLEMENTED,
    JEV_PIN_BOOT_STRAP,
    JEV_PIN_USB_CONSOLE,
    JEV_PIN_FLASH_OR_PSRAM,
} jev_pin_reason_t;

/*
 * Conservative policy for an ESP32-S3-MINI-1 baseline using USB Serial/JTAG.
 * This is a firmware safety policy, not a replacement for the board schematic.
 */
jev_pin_reason_t jev_minimal_esp32s3_pin_reason(int gpio);
bool jev_minimal_esp32s3_pin_is_usable(int gpio);
const char *jev_pin_reason_string(jev_pin_reason_t reason);

#ifdef __cplusplus
}
#endif

