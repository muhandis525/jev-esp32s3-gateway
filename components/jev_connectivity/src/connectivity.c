#include "jev/connectivity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "jev/identity.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

#define ONLINE_BIT BIT0

static const char *TAG = "jev_connectivity";
static EventGroupHandle_t state_events;
static bool normal_mode;

static void delayed_restart(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void delayed_factory_restart(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static esp_err_t provisioning_info_handler(uint32_t session_id,
                                           const uint8_t *input, ssize_t input_length,
                                           uint8_t **output, ssize_t *output_length,
                                           void *private_data)
{
    (void)session_id;
    (void)input;
    (void)input_length;
    (void)private_data;

    size_t capacity = 128;
    char *json = malloc(capacity);
    if (json == NULL) return ESP_ERR_NO_MEM;
    int length = snprintf(json, capacity,
                          "{\"device_id\":\"%s\",\"api_token\":\"%s\",\"api\":1}",
                          jev_identity_device_id(), jev_identity_api_token());
    if (length < 0 || (size_t)length >= capacity) {
        free(json);
        return ESP_ERR_NO_MEM;
    }
    *output = (uint8_t *)json;
    *output_length = length + 1;
    return ESP_OK;
}

static void event_handler(void *argument, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    (void)argument;
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got_ip = event_data;
        xEventGroupSetBits(state_events, ONLINE_BIT);
        printf("{\"event\":\"wifi_online\",\"ip\":\"" IPSTR "\"}\n",
               IP2STR(&got_ip->ip_info.ip));
        return;
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(state_events, ONLINE_BIT);
        if (normal_mode) esp_wifi_connect();
        return;
    }
    if (event_base != NETWORK_PROV_EVENT) return;

    switch (event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "secure BLE Wi-Fi provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            /* Never log the SSID or password. */
            ESP_LOGI(TAG, "Wi-Fi credentials received");
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGW(TAG, "Wi-Fi credentials rejected; provisioning remains available");
            network_prov_mgr_reset_wifi_sm_state_on_failure();
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "provisioning succeeded; restarting into connected mode");
            xTaskCreate(delayed_restart, "prov_restart", 2048, NULL, 5, NULL);
            break;
        case NETWORK_PROV_END:
            network_prov_mgr_deinit();
            break;
        default:
            break;
    }
}

static esp_err_t start_wifi_station(void)
{
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
    return esp_wifi_connect();
}

esp_err_t jev_connectivity_start(bool *provisioning_active)
{
    if (provisioning_active == NULL) return ESP_ERR_INVALID_ARG;
    state_events = xEventGroupCreate();
    if (state_events == NULL) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "network interface init");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (esp_netif_create_default_wifi_sta() == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   event_handler, NULL), TAG, "IP handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                   event_handler, NULL), TAG, "Wi-Fi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                   event_handler, NULL), TAG, "provisioning handler");

    network_prov_mgr_config_t manager_config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
        .network_prov_wifi_conn_cfg = {.wifi_conn_attempts = 5},
    };
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(manager_config), TAG, "provisioning manager init");

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned),
                        TAG, "read provisioning state");
    *provisioning_active = !provisioned;

    if (provisioned) {
        normal_mode = true;
        ESP_RETURN_ON_ERROR(network_prov_mgr_deinit(), TAG, "provisioning manager deinit");
        return start_wifi_station();
    }

    uint8_t service_uuid[16] = {
        0x4a, 0x45, 0x56, 0x2d, 0x53, 0x33, 0x2d, 0x50,
        0x52, 0x4f, 0x56, 0x00, 0x00, 0x00, 0x00, 0x01,
    };
    ESP_RETURN_ON_ERROR(network_prov_scheme_ble_set_service_uuid(service_uuid),
                        TAG, "set BLE provisioning UUID");
    ESP_RETURN_ON_ERROR(network_prov_mgr_endpoint_create("custom-data"),
                        TAG, "create identity endpoint");

    const char *pop = jev_identity_provisioning_pop();
    network_prov_security1_params_t *security_parameters = (void *)pop;
    ESP_RETURN_ON_ERROR(
        network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,
                                            security_parameters,
                                            jev_identity_device_id(), NULL),
        TAG, "start BLE provisioning");
    ESP_RETURN_ON_ERROR(network_prov_mgr_endpoint_register(
                            "custom-data", provisioning_info_handler, NULL),
                        TAG, "register identity endpoint");

    printf("{\"event\":\"provisioning\",\"transport\":\"ble\","
           "\"name\":\"%s\",\"pop\":\"%s\",\"api_token\":\"%s\"}\n",
           jev_identity_device_id(), pop, jev_identity_api_token());
    return ESP_OK;
}

bool jev_connectivity_is_online(void)
{
    return state_events != NULL &&
           (xEventGroupGetBits(state_events) & ONLINE_BIT) != 0;
}

bool jev_connectivity_wait_online(TickType_t timeout)
{
    if (state_events == NULL) return false;
    return (xEventGroupWaitBits(state_events, ONLINE_BIT, pdFALSE, pdTRUE, timeout) & ONLINE_BIT) != 0;
}

int8_t jev_connectivity_rssi(void)
{
    wifi_ap_record_t access_point;
    if (!jev_connectivity_is_online() || esp_wifi_sta_get_ap_info(&access_point) != ESP_OK) {
        return -127;
    }
    return access_point.rssi;
}

esp_err_t jev_connectivity_factory_reset(void)
{
    esp_err_t err = esp_wifi_restore();
    if (err != ESP_OK) return err;
    return xTaskCreate(delayed_factory_restart, "factory_restart", 2048,
                       NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
