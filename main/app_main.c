#include <stdio.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "jev/ble_control.h"
#include "jev/command.h"
#include "jev/connectivity.h"
#include "jev/engine.h"
#include "jev/http_api.h"
#include "jev/identity.h"
#include "jev/tools.h"

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void serial_console_task(void *argument)
{
    (void)argument;
    char line[JEV_COMMAND_MAX_LENGTH + 1];
    char response[JEV_RESPONSE_MAX_LENGTH];
    size_t line_length = 0;
    bool line_overflow = false;
    bool prompt_visible = false;

    while (true) {
        if (!prompt_visible) {
            fputs("jev> ", stdout);
            fflush(stdout);
            prompt_visible = true;
        }
        int byte = fgetc(stdin);
        if (byte == EOF) {
            /* The default UART VFS is nonblocking while no bytes are ready. */
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (byte == '\r') continue;
        if (byte != '\n') {
            if (byte == '\b' || byte == 0x7f) {
                if (line_length > 0) --line_length;
            } else if (!line_overflow) {
                if (line_length < JEV_COMMAND_MAX_LENGTH) {
                    line[line_length++] = (char)byte;
                } else {
                    line_overflow = true;
                }
            }
            continue;
        }

        prompt_visible = false;
        if (line_overflow) {
            puts("{\"ok\":false,\"error\":\"invalid_length\"}");
            line_length = 0;
            line_overflow = false;
            continue;
        }
        if (line_length == 0) continue;
        line[line_length] = '\0';
        line_length = 0;
        jev_engine_execute(line, response, sizeof(response));
        puts(response);
    }
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(jev_identity_init());
    ESP_ERROR_CHECK(jev_tools_init());
    ESP_ERROR_CHECK(jev_engine_init());

    printf("{\"event\":\"ready\",\"product\":\"jev-esp32s3\","
           "\"device_id\":\"%s\",\"hint\":\"type help\"}\n",
           jev_identity_device_id());
    BaseType_t task_created = xTaskCreate(serial_console_task, "jev_console", 4096,
                                          NULL, 4, NULL);
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    bool provisioning_active = false;
    ESP_ERROR_CHECK(jev_connectivity_start(&provisioning_active));
    if (provisioning_active) return;

    /* BLE local control remains available even while the access point is down. */
    ESP_ERROR_CHECK(jev_ble_control_start());
    if (jev_connectivity_wait_online(portMAX_DELAY)) {
        ESP_ERROR_CHECK(jev_http_api_start());
        printf("{\"event\":\"network_ready\",\"device_id\":\"%s\","
               "\"http_port\":80,\"ble\":true}\n",
               jev_identity_device_id());
    }
}
