#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t jev_connectivity_start(bool *provisioning_active);
bool jev_connectivity_is_online(void);
bool jev_connectivity_wait_online(TickType_t timeout);
int8_t jev_connectivity_rssi(void);
esp_err_t jev_connectivity_factory_reset(void);

#ifdef __cplusplus
}
#endif
