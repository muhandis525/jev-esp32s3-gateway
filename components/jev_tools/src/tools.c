#include "jev/tools.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "jev/pin_policy.h"

typedef enum {
    PIN_DIGITAL_INPUT,
    PIN_DIGITAL_OUTPUT,
    PIN_ANALOG_INPUT,
    PIN_PWM_OUTPUT,
    PIN_SERVO_OUTPUT,
} pin_capability_t;

typedef struct {
    const char *name;
    int gpio;
    pin_capability_t capability;
} profile_pin_t;

static const profile_pin_t PROFILE[] = {
    {"led", CONFIG_JEV_LED_GPIO, PIN_DIGITAL_OUTPUT},
    {"relay", CONFIG_JEV_RELAY_GPIO, PIN_DIGITAL_OUTPUT},
    {"button", CONFIG_JEV_BUTTON_GPIO, PIN_DIGITAL_INPUT},
    {"analog", CONFIG_JEV_ANALOG_GPIO, PIN_ANALOG_INPUT},
    {"fan", CONFIG_JEV_PWM_GPIO, PIN_PWM_OUTPUT},
    {"servo", CONFIG_JEV_SERVO_GPIO, PIN_SERVO_OUTPUT},
};

static bool pwm_ready;
static bool servo_ready;
static const char *TAG = "jev_tools";

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool truncated;
} response_writer_t;

static void response_write(response_writer_t *writer, const char *format, ...)
{
    if (writer->truncated || writer->length >= writer->capacity) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->data + writer->length,
                            writer->capacity - writer->length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= writer->capacity - writer->length) {
        writer->data[writer->capacity - 1] = '\0';
        writer->truncated = true;
        return;
    }
    writer->length += (size_t)written;
}

static esp_err_t validate_profile(void)
{
    int used[sizeof(PROFILE) / sizeof(PROFILE[0]) + 2];
    size_t used_count = 0;

    for (size_t i = 0; i < sizeof(PROFILE) / sizeof(PROFILE[0]); ++i) {
        int gpio = PROFILE[i].gpio;
        if (gpio < 0) continue;

        jev_pin_reason_t reason = jev_minimal_esp32s3_pin_reason(gpio);
        if (reason != JEV_PIN_OK) {
            ESP_LOGE(TAG, "target '%s' uses unsafe GPIO%d: %s",
                     PROFILE[i].name, gpio, jev_pin_reason_string(reason));
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < used_count; ++j) {
            if (used[j] == gpio) {
                ESP_LOGE(TAG, "GPIO%d is assigned more than once", gpio);
                return ESP_ERR_INVALID_ARG;
            }
        }
        used[used_count++] = gpio;
    }

    int i2c_pins[] = {CONFIG_JEV_I2C_SDA_GPIO, CONFIG_JEV_I2C_SCL_GPIO};
    if ((i2c_pins[0] < 0) != (i2c_pins[1] < 0)) {
        ESP_LOGE(TAG, "I2C SDA and SCL must both be enabled or both disabled");
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(i2c_pins) / sizeof(i2c_pins[0]); ++i) {
        int gpio = i2c_pins[i];
        if (gpio < 0) continue;
        jev_pin_reason_t reason = jev_minimal_esp32s3_pin_reason(gpio);
        if (reason != JEV_PIN_OK) {
            ESP_LOGE(TAG, "I2C uses unsafe GPIO%d: %s", gpio, jev_pin_reason_string(reason));
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < used_count; ++j) {
            if (used[j] == gpio) {
                ESP_LOGE(TAG, "GPIO%d is assigned more than once", gpio);
                return ESP_ERR_INVALID_ARG;
            }
        }
        used[used_count++] = gpio;
    }
    return ESP_OK;
}

static const profile_pin_t *find_pin(const char *name, pin_capability_t capability)
{
    for (size_t i = 0; i < sizeof(PROFILE) / sizeof(PROFILE[0]); ++i) {
        if (PROFILE[i].gpio >= 0 && PROFILE[i].capability == capability &&
            strcmp(PROFILE[i].name, name) == 0) {
            return &PROFILE[i];
        }
    }
    return NULL;
}

static esp_err_t write_digital(const jev_command_t *command, response_writer_t *output)
{
    const profile_pin_t *pin = find_pin(command->target, PIN_DIGITAL_OUTPUT);
    if (pin == NULL) return ESP_ERR_NOT_FOUND;
    ESP_RETURN_ON_ERROR(gpio_set_level(pin->gpio, command->value), "jev_tools", "gpio write");
    response_write(output, "{\"ok\":true,\"tool\":\"digital.write\",\"target\":\"%s\",\"value\":%ld}",
                   pin->name, (long)command->value);
    return ESP_OK;
}

static esp_err_t read_digital(const jev_command_t *command, response_writer_t *output)
{
    const profile_pin_t *pin = find_pin(command->target, PIN_DIGITAL_INPUT);
    if (pin == NULL) return ESP_ERR_NOT_FOUND;
    int level = gpio_get_level(pin->gpio);
    response_write(output, "{\"ok\":true,\"tool\":\"digital.read\",\"target\":\"%s\",\"value\":%d}",
                   pin->name, level);
    return ESP_OK;
}

static esp_err_t read_analog(const jev_command_t *command, response_writer_t *output)
{
    const profile_pin_t *pin = find_pin(command->target, PIN_ANALOG_INPUT);
    if (pin == NULL) return ESP_ERR_NOT_FOUND;

    adc_unit_t unit;
    adc_channel_t channel;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(pin->gpio, &unit, &channel),
                        "jev_tools", "ADC GPIO mapping");

    adc_oneshot_unit_handle_t handle;
    adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = unit};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_config, &handle),
                        "jev_tools", "ADC unit init");

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_oneshot_config_channel(handle, channel, &channel_config);
    int raw = 0;
    if (err == ESP_OK) err = adc_oneshot_read(handle, channel, &raw);
    adc_oneshot_del_unit(handle);
    if (err != ESP_OK) return err;

    response_write(output, "{\"ok\":true,\"tool\":\"analog.read\",\"target\":\"%s\",\"raw\":%d}",
                   pin->name, raw);
    return ESP_OK;
}

static esp_err_t set_pwm(const jev_command_t *command, response_writer_t *output)
{
    const profile_pin_t *pin = find_pin(command->target, PIN_PWM_OUTPUT);
    if (pin == NULL) return ESP_ERR_NOT_FOUND;

    if (!pwm_ready) {
        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_12_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "jev_tools", "PWM timer");
        ledc_channel_config_t channel = {
            .gpio_num = pin->gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "jev_tools", "PWM channel");
        pwm_ready = true;
    }

    uint32_t duty = ((uint32_t)command->value * 4095U) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty),
                        "jev_tools", "PWM duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0),
                        "jev_tools", "PWM update");
    response_write(output, "{\"ok\":true,\"tool\":\"pwm.set\",\"target\":\"%s\",\"percent\":%ld}",
                   pin->name, (long)command->value);
    return ESP_OK;
}

static esp_err_t set_servo(const jev_command_t *command, response_writer_t *output)
{
    const profile_pin_t *pin = find_pin(command->target, PIN_SERVO_OUTPUT);
    if (pin == NULL) return ESP_ERR_NOT_FOUND;

    if (!servo_ready) {
        ledc_timer_config_t timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_14_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 50,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), "jev_tools", "servo timer");
        ledc_channel_config_t channel = {
            .gpio_num = pin->gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .timer_sel = LEDC_TIMER_1,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), "jev_tools", "servo channel");
        servo_ready = true;
    }

    uint32_t pulse_us = 500U + ((uint32_t)command->value * 2000U) / 180U;
    uint32_t duty = (pulse_us * 16383U) / 20000U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty),
                        "jev_tools", "servo duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1),
                        "jev_tools", "servo update");
    response_write(output, "{\"ok\":true,\"tool\":\"servo.set\",\"target\":\"%s\",\"degrees\":%ld}",
                   pin->name, (long)command->value);
    return ESP_OK;
}

static esp_err_t scan_i2c(response_writer_t *output)
{
    if (CONFIG_JEV_I2C_SDA_GPIO < 0 || CONFIG_JEV_I2C_SCL_GPIO < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_JEV_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_JEV_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &bus), "jev_tools", "I2C bus");

    response_write(output, "{\"ok\":true,\"tool\":\"i2c.scan\",\"addresses\":[");
    bool first = true;
    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        if (i2c_master_probe(bus, address, 20) == ESP_OK) {
            response_write(output, "%s%u", first ? "" : ",", address);
            first = false;
        }
    }
    response_write(output, "]}");
    i2c_del_master_bus(bus);
    return ESP_OK;
}

static esp_err_t stop_outputs(response_writer_t *output)
{
    for (size_t i = 0; i < sizeof(PROFILE) / sizeof(PROFILE[0]); ++i) {
        if (PROFILE[i].gpio >= 0 && PROFILE[i].capability == PIN_DIGITAL_OUTPUT) {
            gpio_set_level(PROFILE[i].gpio, 0);
        }
    }
    if (pwm_ready) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    response_write(output, "{\"ok\":true,\"tool\":\"stop\",\"outputs\":\"safe\"}");
    return ESP_OK;
}

esp_err_t jev_tools_init(void)
{
    ESP_RETURN_ON_ERROR(validate_profile(), TAG, "invalid board profile");

    uint64_t output_mask = 0;
    uint64_t input_mask = 0;
    for (size_t i = 0; i < sizeof(PROFILE) / sizeof(PROFILE[0]); ++i) {
        if (PROFILE[i].gpio < 0) continue;
        if (PROFILE[i].capability == PIN_DIGITAL_OUTPUT) output_mask |= 1ULL << PROFILE[i].gpio;
        if (PROFILE[i].capability == PIN_DIGITAL_INPUT) input_mask |= 1ULL << PROFILE[i].gpio;
    }
    if (output_mask != 0) {
        gpio_config_t config = {
            .pin_bit_mask = output_mask,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&config), "jev_tools", "output GPIO config");
        for (size_t i = 0; i < sizeof(PROFILE) / sizeof(PROFILE[0]); ++i) {
            if (PROFILE[i].gpio >= 0 && PROFILE[i].capability == PIN_DIGITAL_OUTPUT) {
                gpio_set_level(PROFILE[i].gpio, 0);
            }
        }
    }
    if (input_mask != 0) {
        gpio_config_t config = {
            .pin_bit_mask = input_mask,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&config), "jev_tools", "input GPIO config");
    }
    return ESP_OK;
}

esp_err_t jev_tools_execute(const jev_command_t *command, char *output, size_t output_size)
{
    if (command == NULL || output == NULL || output_size < 2) return ESP_ERR_INVALID_ARG;
    response_writer_t writer = {
        .data = output,
        .capacity = output_size,
        .length = 0,
        .truncated = false,
    };
    output[0] = '\0';
    switch (command->kind) {
        case JEV_COMMAND_HELP:
            response_write(&writer, "{\"ok\":true,\"commands\":[\"help\",\"system info\",\"digital read <target>\",\"digital write <target> <on|off>\",\"analog read <target>\",\"pwm set <target> <0..100>\",\"servo set <target> <0..180>\",\"i2c scan\",\"network reset\",\"stop\"]}");
            break;
        case JEV_COMMAND_SYSTEM_INFO: {
            esp_chip_info_t chip;
            uint32_t flash_size = 0;
            esp_chip_info(&chip);
            esp_flash_get_size(NULL, &flash_size);
            response_write(&writer, "{\"ok\":true,\"tool\":\"system.info\",\"cores\":%d,\"revision\":%d,\"flash_bytes\":%lu}",
                           chip.cores, chip.revision, (unsigned long)flash_size);
            break;
        }
        case JEV_COMMAND_DIGITAL_READ: return read_digital(command, &writer);
        case JEV_COMMAND_DIGITAL_WRITE: return write_digital(command, &writer);
        case JEV_COMMAND_ANALOG_READ: return read_analog(command, &writer);
        case JEV_COMMAND_PWM_SET: return set_pwm(command, &writer);
        case JEV_COMMAND_SERVO_SET: return set_servo(command, &writer);
        case JEV_COMMAND_I2C_SCAN: return scan_i2c(&writer);
        case JEV_COMMAND_NETWORK_RESET: return ESP_ERR_NOT_SUPPORTED;
        case JEV_COMMAND_STOP: return stop_outputs(&writer);
        default: return ESP_ERR_INVALID_ARG;
    }
    return writer.truncated ? ESP_ERR_NO_MEM : ESP_OK;
}
