#include "jev/pin_policy.h"

jev_pin_reason_t jev_minimal_esp32s3_pin_reason(int gpio)
{
    if (gpio < 0 || gpio > 48) {
        return JEV_PIN_OUT_OF_RANGE;
    }
    if (gpio >= 22 && gpio <= 25) {
        return JEV_PIN_NOT_IMPLEMENTED;
    }
    if (gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46) {
        return JEV_PIN_BOOT_STRAP;
    }
    if (gpio == 19 || gpio == 20) {
        return JEV_PIN_USB_CONSOLE;
    }
    if (gpio >= 26 && gpio <= 32) {
        return JEV_PIN_FLASH_OR_PSRAM;
    }
    return JEV_PIN_OK;
}

bool jev_minimal_esp32s3_pin_is_usable(int gpio)
{
    return jev_minimal_esp32s3_pin_reason(gpio) == JEV_PIN_OK;
}

const char *jev_pin_reason_string(jev_pin_reason_t reason)
{
    switch (reason) {
        case JEV_PIN_OK: return "usable";
        case JEV_PIN_OUT_OF_RANGE: return "outside ESP32-S3 GPIO range";
        case JEV_PIN_NOT_IMPLEMENTED: return "not implemented on ESP32-S3";
        case JEV_PIN_BOOT_STRAP: return "reserved boot-strapping pin";
        case JEV_PIN_USB_CONSOLE: return "reserved for USB Serial/JTAG console";
        case JEV_PIN_FLASH_OR_PSRAM: return "reserved for flash or PSRAM";
        default: return "unknown pin restriction";
    }
}
