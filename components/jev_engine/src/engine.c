#include "jev/engine.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "jev/command.h"
#include "jev/connectivity.h"
#include "jev/tools.h"

static SemaphoreHandle_t engine_mutex;

esp_err_t jev_engine_init(void)
{
    if (engine_mutex != NULL) return ESP_OK;
    engine_mutex = xSemaphoreCreateMutex();
    return engine_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t jev_engine_execute(const char *line, char *response, size_t response_size)
{
    if (line == NULL || response == NULL || response_size < 2 || engine_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    jev_command_t command;
    jev_parse_error_t parse_error;
    if (!jev_command_parse(line, &command, &parse_error)) {
        snprintf(response, response_size,
                 "{\"ok\":false,\"error\":\"parse\",\"detail\":\"%s\"}",
                 jev_parse_error_string(parse_error));
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(engine_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        snprintf(response, response_size,
                 "{\"ok\":false,\"error\":\"busy\"}");
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err;
    if (command.kind == JEV_COMMAND_NETWORK_RESET) {
        err = jev_connectivity_factory_reset();
        if (err == ESP_OK) {
            snprintf(response, response_size,
                     "{\"ok\":true,\"tool\":\"network.reset\",\"restarting\":true}");
        } else {
            snprintf(response, response_size,
                     "{\"ok\":false,\"error\":\"network_reset_failed\",\"code\":%ld}",
                     (long)err);
        }
    } else {
        err = jev_tools_execute(&command, response, response_size);
    }
    xSemaphoreGive(engine_mutex);

    if (err != ESP_OK && command.kind != JEV_COMMAND_NETWORK_RESET) {
        snprintf(response, response_size,
                 "{\"ok\":false,\"error\":\"tool_failed\",\"code\":%ld}",
                 (long)err);
    }
    return err;
}
