#include "jev/identity.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"

static char device_id[JEV_DEVICE_ID_MAX + 1];
static char provisioning_pop[JEV_PROV_POP_LENGTH + 1];
static char api_token[JEV_API_TOKEN_LENGTH + 1];

static void random_hex(char *output, size_t digits)
{
    static const char HEX[] = "0123456789abcdef";
    uint8_t random_bytes[(JEV_API_TOKEN_LENGTH + 1) / 2];
    size_t byte_count = (digits + 1) / 2;
    esp_fill_random(random_bytes, byte_count);
    for (size_t i = 0; i < digits; ++i) {
        uint8_t value = random_bytes[i / 2];
        output[i] = HEX[(i & 1U) == 0 ? value >> 4 : value & 0x0f];
    }
    output[digits] = '\0';
}

static esp_err_t get_or_create(nvs_handle_t handle, const char *key,
                               char *output, size_t output_size, size_t digits)
{
    size_t stored_size = output_size;
    esp_err_t err = nvs_get_str(handle, key, output, &stored_size);
    if (err == ESP_OK && stored_size == digits + 1) return ESP_OK;
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH && err != ESP_OK) {
        return err;
    }
    random_hex(output, digits);
    return nvs_set_str(handle, key, output);
}

esp_err_t jev_identity_init(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(device_id, sizeof(device_id), "JEV-%02X%02X%02X", mac[3], mac[4], mac[5]);

    nvs_handle_t handle;
    err = nvs_open("jev", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = get_or_create(handle, "prov_pop", provisioning_pop,
                        sizeof(provisioning_pop), JEV_PROV_POP_LENGTH);
    if (err == ESP_OK) {
        err = get_or_create(handle, "api_token", api_token,
                            sizeof(api_token), JEV_API_TOKEN_LENGTH);
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

const char *jev_identity_device_id(void)
{
    return device_id;
}

const char *jev_identity_provisioning_pop(void)
{
    return provisioning_pop;
}

const char *jev_identity_api_token(void)
{
    return api_token;
}

bool jev_identity_token_matches(const char *candidate, size_t candidate_length)
{
    if (candidate == NULL || candidate_length != JEV_API_TOKEN_LENGTH) return false;
    unsigned int difference = 0;
    for (size_t i = 0; i < JEV_API_TOKEN_LENGTH; ++i) {
        difference |= (unsigned char)candidate[i] ^ (unsigned char)api_token[i];
    }
    return difference == 0;
}
