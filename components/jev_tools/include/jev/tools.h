#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "jev/command.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t jev_tools_init(void);
esp_err_t jev_tools_execute(const jev_command_t *command, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif
