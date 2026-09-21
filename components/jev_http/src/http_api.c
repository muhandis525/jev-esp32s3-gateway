#include "jev/http_api.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "jev/command.h"
#include "jev/connectivity.h"
#include "jev/engine.h"
#include "jev/identity.h"

static httpd_handle_t server;

static esp_err_t send_json(httpd_req_t *request, const char *body)
{
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

static bool authorized(httpd_req_t *request)
{
    char header[8 + JEV_API_TOKEN_LENGTH];
    size_t length = httpd_req_get_hdr_value_len(request, "Authorization");
    if (length != 7 + JEV_API_TOKEN_LENGTH || length >= sizeof(header)) return false;
    if (httpd_req_get_hdr_value_str(request, "Authorization", header, sizeof(header)) != ESP_OK) {
        return false;
    }
    return memcmp(header, "Bearer ", 7) == 0 &&
           jev_identity_token_matches(header + 7, JEV_API_TOKEN_LENGTH);
}

static esp_err_t require_authorization(httpd_req_t *request)
{
    if (authorized(request)) return ESP_OK;
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_hdr(request, "WWW-Authenticate", "Bearer");
    send_json(request, "{\"ok\":false,\"error\":\"unauthorized\"}");
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t health_handler(httpd_req_t *request)
{
    return send_json(request, "{\"ok\":true}");
}

static esp_err_t status_handler(httpd_req_t *request)
{
    if (require_authorization(request) != ESP_OK) return ESP_OK;
    char response[256];
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"device_id\":\"%s\",\"wifi\":%s,"
             "\"rssi\":%d,\"uptime_ms\":%llu,\"free_heap\":%lu}",
             jev_identity_device_id(),
             jev_connectivity_is_online() ? "true" : "false",
             jev_connectivity_rssi(),
             (unsigned long long)(esp_timer_get_time() / 1000),
             (unsigned long)esp_get_free_heap_size());
    return send_json(request, response);
}

static esp_err_t command_handler(httpd_req_t *request)
{
    if (require_authorization(request) != ESP_OK) return ESP_OK;
    if (request->content_len <= 0 || request->content_len > JEV_COMMAND_MAX_LENGTH) {
        httpd_resp_set_status(request, "413 Payload Too Large");
        return send_json(request, "{\"ok\":false,\"error\":\"invalid_length\"}");
    }

    char command[JEV_COMMAND_MAX_LENGTH + 1];
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(request, command + received,
                                    request->content_len - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (result <= 0) {
            httpd_resp_set_status(request, "400 Bad Request");
            return send_json(request, "{\"ok\":false,\"error\":\"receive_failed\"}");
        }
        received += (size_t)result;
    }
    command[received] = '\0';

    char response[JEV_RESPONSE_MAX_LENGTH];
    esp_err_t result = jev_engine_execute(command, response, sizeof(response));
    if (result != ESP_OK) httpd_resp_set_status(request, "422 Unprocessable Entity");
    return send_json(request, response);
}

esp_err_t jev_http_api_start(void)
{
    if (server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 6144;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t routes[] = {
        {.uri = "/healthz", .method = HTTP_GET, .handler = health_handler},
        {.uri = "/api/v1/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/v1/command", .method = HTTP_POST, .handler = command_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            jev_http_api_stop();
            return err;
        }
    }
    return ESP_OK;
}

void jev_http_api_stop(void)
{
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
    }
}
