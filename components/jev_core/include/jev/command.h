#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JEV_COMMAND_MAX_LENGTH 127
#define JEV_TARGET_MAX_LENGTH  15

typedef enum {
    JEV_COMMAND_HELP = 0,
    JEV_COMMAND_SYSTEM_INFO,
    JEV_COMMAND_DIGITAL_READ,
    JEV_COMMAND_DIGITAL_WRITE,
    JEV_COMMAND_ANALOG_READ,
    JEV_COMMAND_PWM_SET,
    JEV_COMMAND_SERVO_SET,
    JEV_COMMAND_I2C_SCAN,
    JEV_COMMAND_NETWORK_RESET,
    JEV_COMMAND_STOP,
} jev_command_kind_t;

typedef struct {
    jev_command_kind_t kind;
    char target[JEV_TARGET_MAX_LENGTH + 1];
    int32_t value;
} jev_command_t;

typedef enum {
    JEV_PARSE_OK = 0,
    JEV_PARSE_EMPTY,
    JEV_PARSE_TOO_LONG,
    JEV_PARSE_TOO_MANY_TOKENS,
    JEV_PARSE_UNKNOWN_COMMAND,
    JEV_PARSE_MISSING_ARGUMENT,
    JEV_PARSE_EXTRA_ARGUMENT,
    JEV_PARSE_BAD_TARGET,
    JEV_PARSE_BAD_VALUE,
    JEV_PARSE_OUT_OF_RANGE,
} jev_parse_error_t;

bool jev_command_parse(const char *input, jev_command_t *out, jev_parse_error_t *error);
const char *jev_parse_error_string(jev_parse_error_t error);

#ifdef __cplusplus
}
#endif
