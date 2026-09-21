#include "jev/command.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 5

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool equals(const char *left, const char *right)
{
    return strcmp(left, right) == 0;
}

static bool valid_target(const char *target)
{
    size_t length = strlen(target);
    if (length == 0 || length > JEV_TARGET_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)target[i];
        if (!(isalnum(ch) || ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

static bool parse_integer(const char *text, int32_t minimum, int32_t maximum,
                          int32_t *value, jev_parse_error_t *error)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || parsed < INT32_MIN || parsed > INT32_MAX) {
        *error = JEV_PARSE_OUT_OF_RANGE;
        return false;
    }
    if (end == text || *end != '\0') {
        *error = JEV_PARSE_BAD_VALUE;
        return false;
    }
    if (parsed < minimum || parsed > maximum) {
        *error = JEV_PARSE_OUT_OF_RANGE;
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static bool expect_count(size_t actual, size_t expected, jev_parse_error_t *error)
{
    if (actual < expected) {
        *error = JEV_PARSE_MISSING_ARGUMENT;
        return false;
    }
    if (actual > expected) {
        *error = JEV_PARSE_EXTRA_ARGUMENT;
        return false;
    }
    return true;
}

static bool set_target(jev_command_t *out, const char *target, jev_parse_error_t *error)
{
    if (!valid_target(target)) {
        *error = JEV_PARSE_BAD_TARGET;
        return false;
    }
    memcpy(out->target, target, strlen(target) + 1);
    return true;
}

bool jev_command_parse(const char *input, jev_command_t *out, jev_parse_error_t *error)
{
    char buffer[JEV_COMMAND_MAX_LENGTH + 1];
    char *tokens[MAX_TOKENS];
    size_t count = 0;

    if (error != NULL) {
        *error = JEV_PARSE_OK;
    }
    if (input == NULL || out == NULL || error == NULL) {
        return false;
    }

    size_t input_length = bounded_length(input, JEV_COMMAND_MAX_LENGTH + 1);
    if (input_length > JEV_COMMAND_MAX_LENGTH) {
        *error = JEV_PARSE_TOO_LONG;
        return false;
    }

    memcpy(buffer, input, input_length + 1);
    for (size_t i = 0; i < input_length; ++i) {
        buffer[i] = (char)tolower((unsigned char)buffer[i]);
    }

    char *cursor = buffer;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (count == MAX_TOKENS) {
            *error = JEV_PARSE_TOO_MANY_TOKENS;
            return false;
        }
        tokens[count++] = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    if (count == 0) {
        *error = JEV_PARSE_EMPTY;
        return false;
    }

    memset(out, 0, sizeof(*out));

    if (equals(tokens[0], "help")) {
        if (!expect_count(count, 1, error)) return false;
        out->kind = JEV_COMMAND_HELP;
        return true;
    }
    if (equals(tokens[0], "stop")) {
        if (!expect_count(count, 1, error)) return false;
        out->kind = JEV_COMMAND_STOP;
        return true;
    }
    if (equals(tokens[0], "system")) {
        if (!expect_count(count, 2, error)) return false;
        if (!equals(tokens[1], "info")) goto unknown;
        out->kind = JEV_COMMAND_SYSTEM_INFO;
        return true;
    }
    if (equals(tokens[0], "network")) {
        if (!expect_count(count, 2, error)) return false;
        if (!equals(tokens[1], "reset")) goto unknown;
        out->kind = JEV_COMMAND_NETWORK_RESET;
        return true;
    }
    if (equals(tokens[0], "digital")) {
        if (count < 2) {
            *error = JEV_PARSE_MISSING_ARGUMENT;
            return false;
        }
        if (equals(tokens[1], "read")) {
            if (!expect_count(count, 3, error)) return false;
            out->kind = JEV_COMMAND_DIGITAL_READ;
            return set_target(out, tokens[2], error);
        }
        if (equals(tokens[1], "write")) {
            if (!expect_count(count, 4, error)) return false;
            out->kind = JEV_COMMAND_DIGITAL_WRITE;
            if (!set_target(out, tokens[2], error)) return false;
            if (equals(tokens[3], "on") || equals(tokens[3], "high") || equals(tokens[3], "1")) {
                out->value = 1;
                return true;
            }
            if (equals(tokens[3], "off") || equals(tokens[3], "low") || equals(tokens[3], "0")) {
                out->value = 0;
                return true;
            }
            *error = JEV_PARSE_BAD_VALUE;
            return false;
        }
        goto unknown;
    }
    if (equals(tokens[0], "analog")) {
        if (!expect_count(count, 3, error)) return false;
        if (!equals(tokens[1], "read")) goto unknown;
        out->kind = JEV_COMMAND_ANALOG_READ;
        return set_target(out, tokens[2], error);
    }
    if (equals(tokens[0], "pwm")) {
        if (!expect_count(count, 4, error)) return false;
        if (!equals(tokens[1], "set")) goto unknown;
        out->kind = JEV_COMMAND_PWM_SET;
        if (!set_target(out, tokens[2], error)) return false;
        return parse_integer(tokens[3], 0, 100, &out->value, error);
    }
    if (equals(tokens[0], "servo")) {
        if (!expect_count(count, 4, error)) return false;
        if (!equals(tokens[1], "set")) goto unknown;
        out->kind = JEV_COMMAND_SERVO_SET;
        if (!set_target(out, tokens[2], error)) return false;
        return parse_integer(tokens[3], 0, 180, &out->value, error);
    }
    if (equals(tokens[0], "i2c")) {
        if (!expect_count(count, 2, error)) return false;
        if (!equals(tokens[1], "scan")) goto unknown;
        out->kind = JEV_COMMAND_I2C_SCAN;
        return true;
    }

unknown:
    *error = JEV_PARSE_UNKNOWN_COMMAND;
    return false;
}

const char *jev_parse_error_string(jev_parse_error_t error)
{
    switch (error) {
        case JEV_PARSE_OK: return "ok";
        case JEV_PARSE_EMPTY: return "empty command";
        case JEV_PARSE_TOO_LONG: return "command too long";
        case JEV_PARSE_TOO_MANY_TOKENS: return "too many words";
        case JEV_PARSE_UNKNOWN_COMMAND: return "unknown command";
        case JEV_PARSE_MISSING_ARGUMENT: return "missing argument";
        case JEV_PARSE_EXTRA_ARGUMENT: return "extra argument";
        case JEV_PARSE_BAD_TARGET: return "invalid target name";
        case JEV_PARSE_BAD_VALUE: return "invalid value";
        case JEV_PARSE_OUT_OF_RANGE: return "value out of range";
        default: return "internal parse error";
    }
}
