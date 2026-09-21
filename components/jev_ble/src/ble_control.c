#include "jev/ble_control.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "jev/command.h"
#include "jev/engine.h"
#include "jev/identity.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

static const char *TAG = "jev_ble";
static uint8_t own_address_type;
static uint16_t response_value_handle;
static uint16_t command_value_handle;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static bool response_notifications;
static char last_response[JEV_RESPONSE_MAX_LENGTH] = "{\"ok\":true,\"event\":\"ready\"}";

static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x01, 0x00, 0x00, 0x00, 0x56, 0x45, 0x4a, 0x2d,
    0x43, 0x54, 0x52, 0x4c, 0x2d, 0x53, 0x33, 0x32);
static const ble_uuid128_t command_uuid = BLE_UUID128_INIT(
    0x02, 0x00, 0x00, 0x00, 0x56, 0x45, 0x4a, 0x2d,
    0x43, 0x54, 0x52, 0x4c, 0x2d, 0x53, 0x33, 0x32);
static const ble_uuid128_t response_uuid = BLE_UUID128_INIT(
    0x03, 0x00, 0x00, 0x00, 0x56, 0x45, 0x4a, 0x2d,
    0x43, 0x54, 0x52, 0x4c, 0x2d, 0x53, 0x33, 0x32);

static int response_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(context->om, last_response, strlen(last_response)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static void notify_response(void)
{
    if (!response_notifications || connection_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *packet = ble_hs_mbuf_from_flat(last_response, strlen(last_response));
    if (packet != NULL) {
        int rc = ble_gatts_notify_custom(connection_handle, response_value_handle, packet);
        if (rc != 0) ESP_LOGW(TAG, "response notification failed: %d", rc);
    }
}

static int command_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)conn_handle;
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR || attr_handle != command_value_handle) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    size_t payload_length = OS_MBUF_PKTLEN(context->om);
    if (payload_length <= JEV_API_TOKEN_LENGTH + 1 ||
        payload_length > JEV_API_TOKEN_LENGTH + 1 + JEV_COMMAND_MAX_LENGTH) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    char payload[JEV_API_TOKEN_LENGTH + 1 + JEV_COMMAND_MAX_LENGTH + 1];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, payload, payload_length, &copied) != 0 ||
        copied != payload_length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    payload[payload_length] = '\0';

    if (payload[JEV_API_TOKEN_LENGTH] != '|' ||
        !jev_identity_token_matches(payload, JEV_API_TOKEN_LENGTH)) {
        strlcpy(last_response, "{\"ok\":false,\"error\":\"unauthorized\"}",
                sizeof(last_response));
        notify_response();
        return BLE_ATT_ERR_UNLIKELY;
    }

    jev_engine_execute(payload + JEV_API_TOKEN_LENGTH + 1,
                       last_response, sizeof(last_response));
    notify_response();
    return 0;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &command_uuid.u,
                .access_cb = command_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
                .val_handle = &command_value_handle,
            },
            {
                .uuid = &response_uuid.u,
                .access_cb = response_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &response_value_handle,
            },
            {0},
        },
    },
    {0},
};

static void start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                connection_handle = event->connect.conn_handle;
                ble_gap_security_initiate(connection_handle);
            } else {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
            response_notifications = false;
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == response_value_handle) {
                response_notifications = event->subscribe.cur_notify;
            }
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc description;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) {
                ble_store_util_delete_peer(&description.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)jev_identity_device_id();
    fields.name_len = strlen(jev_identity_device_id());
    fields.name_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields) != 0) return;

    struct ble_gap_adv_params parameters = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(250),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(300),
    };
    int rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER,
                               &parameters, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "advertising failed: %d", rc);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &own_address_type) != 0) {
        ESP_LOGE(TAG, "BLE identity unavailable");
        return;
    }
    start_advertising();
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    vTaskDelete(NULL);
}

esp_err_t jev_ble_control_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(jev_identity_device_id());
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc != 0) return ESP_FAIL;

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_store_config_init();

    return xTaskCreate(host_task, "jev_ble", 4096, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
