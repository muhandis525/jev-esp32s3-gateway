#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the normal-operation BLE command service after Wi-Fi provisioning. */
esp_err_t jev_ble_control_start(void);

#ifdef __cplusplus
}
#endif
