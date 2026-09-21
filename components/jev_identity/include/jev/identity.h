#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JEV_DEVICE_ID_MAX 15
#define JEV_PROV_POP_LENGTH 12
#define JEV_API_TOKEN_LENGTH 32

esp_err_t jev_identity_init(void);
const char *jev_identity_device_id(void);
const char *jev_identity_provisioning_pop(void);
const char *jev_identity_api_token(void);
bool jev_identity_token_matches(const char *candidate, size_t candidate_length);

#ifdef __cplusplus
}
#endif
