#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t jev_http_api_start(void);
void jev_http_api_stop(void);

#ifdef __cplusplus
}
#endif
