#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JEV_RESPONSE_MAX_LENGTH 768

esp_err_t jev_engine_init(void);
esp_err_t jev_engine_execute(const char *line, char *response, size_t response_size);

#ifdef __cplusplus
}
#endif
