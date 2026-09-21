#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jev/command.h"
#include "jev/pin_policy.h"

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void expect_ok(const char *text, jev_command_kind_t kind, const char *target, int value)
{
    jev_command_t command;
    jev_parse_error_t error;
    CHECK(jev_command_parse(text, &command, &error));
    CHECK(error == JEV_PARSE_OK);
    CHECK(command.kind == kind);
    CHECK(strcmp(command.target, target) == 0);
    CHECK(command.value == value);
}

static void expect_error(const char *text, jev_parse_error_t expected)
{
    jev_command_t command;
    jev_parse_error_t error;
    CHECK(!jev_command_parse(text, &command, &error));
    CHECK(error == expected);
}

int main(void)
{
    expect_ok("help\n", JEV_COMMAND_HELP, "", 0);
    expect_ok(" SYSTEM INFO ", JEV_COMMAND_SYSTEM_INFO, "", 0);
    expect_ok("digital read button", JEV_COMMAND_DIGITAL_READ, "button", 0);
    expect_ok("digital write RELAY on", JEV_COMMAND_DIGITAL_WRITE, "relay", 1);
    expect_ok("digital write led 0", JEV_COMMAND_DIGITAL_WRITE, "led", 0);
    expect_ok("analog read soil-1", JEV_COMMAND_ANALOG_READ, "soil-1", 0);
    expect_ok("pwm set fan 73", JEV_COMMAND_PWM_SET, "fan", 73);
    expect_ok("servo set arm 180", JEV_COMMAND_SERVO_SET, "arm", 180);
    expect_ok("i2c scan", JEV_COMMAND_I2C_SCAN, "", 0);
    expect_ok("network reset", JEV_COMMAND_NETWORK_RESET, "", 0);
    expect_ok("stop", JEV_COMMAND_STOP, "", 0);

    expect_error("", JEV_PARSE_EMPTY);
    expect_error("pwm set fan", JEV_PARSE_MISSING_ARGUMENT);
    expect_error("pwm set fan 101", JEV_PARSE_OUT_OF_RANGE);
    expect_error("servo set arm -1", JEV_PARSE_OUT_OF_RANGE);
    expect_error("digital write relay maybe", JEV_PARSE_BAD_VALUE);
    expect_error("digital read bad/target", JEV_PARSE_BAD_TARGET);
    expect_error("i2c scan now", JEV_PARSE_EXTRA_ARGUMENT);
    expect_error("erase flash", JEV_PARSE_UNKNOWN_COMMAND);

    CHECK(jev_minimal_esp32s3_pin_is_usable(1));
    CHECK(jev_minimal_esp32s3_pin_is_usable(21));
    CHECK(jev_minimal_esp32s3_pin_is_usable(33));
    CHECK(jev_minimal_esp32s3_pin_is_usable(48));
    CHECK(jev_minimal_esp32s3_pin_reason(0) == JEV_PIN_BOOT_STRAP);
    CHECK(jev_minimal_esp32s3_pin_reason(19) == JEV_PIN_USB_CONSOLE);
    CHECK(jev_minimal_esp32s3_pin_reason(22) == JEV_PIN_NOT_IMPLEMENTED);
    CHECK(jev_minimal_esp32s3_pin_reason(26) == JEV_PIN_FLASH_OR_PSRAM);
    CHECK(jev_minimal_esp32s3_pin_reason(49) == JEV_PIN_OUT_OF_RANGE);

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("all command parser tests passed");
    return EXIT_SUCCESS;
}
