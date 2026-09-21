# Research and design decisions

Research date: 2026-09-21.

## Closest existing systems

### TypeSafe Jev smart-home demo

This is the closest match. TypeSafe's official demo classifies a request such as “turn off all lights” by asking, in one request, for the request category, domain, device type, and action. It calls this speculative fan-out: ask independent questions in parallel, then use ordinary code to keep only relevant answers. The demo also detects compound requests and sends those to a generative model for splitting. We adopt the batched classification pattern but reject compound actions in the first version instead of silently splitting them.

Sources: [official smart-home demo](https://docs.typesafe.ai/demos/smart-home), [Choice primitive](https://docs.typesafe.ai/primitives/choice), [API reference](https://docs.typesafe.ai/api).

Jev is a decision model, not a text generator. It supports:

- Choice: one value from an explicit option set, plus probabilities and confidence.
- Score: a probability-weighted position on a rubric of at most ten levels.
- Noul: a yes-probability from 0 to 1.

That is a good fit for choosing an allowlisted tool, target, and action. It is not a good fit for generating arbitrary code or exact unbounded numeric arguments. Exact numbers such as `73%` are therefore extracted with deterministic code after Jev selects the tool; otherwise PWM/servo values are quantized from a ten-level Score.

### ESPHome

ESPHome describes devices and automations declaratively and compiles them into firmware. It is excellent when the hardware definition is known ahead of time and integration with Home Assistant is the goal. It is less suitable for this project because we want a stable firmware runtime with a JEV-selected tool surface rather than generating/reflashing firmware for each command.

### Tasmota with Berry

Tasmota on ESP32 embeds Berry, an efficient dynamic language intended for microcontrollers. It supports advanced automation and even custom drivers. This is the strongest off-the-shelf alternative if the requirement is unrestricted on-device scripting. We do not need that power initially: a smaller DSL has less attack surface, deterministic resource use, and a much easier safety review.

Source: [Tasmota Berry documentation](https://tasmota.github.io/docs/Berry/).

### Firmata

Firmata exposes microcontroller pins and peripherals to a host using a compact binary protocol. It proves the generic remote-I/O model, but its raw pin-oriented abstraction grants too much authority for natural-language control. This project exposes semantic targets such as `relay` and `fan`, then maps them to pins in a board profile.

Source: [Firmata protocol](https://github.com/firmata/protocol).

### MicroPython

MicroPython would make rapid sensor-driver development easy, but accepting generated Python would create a large and difficult-to-bound execution surface. ESP-IDF C provides better control over memory, watchdog behavior, peripheral ownership, OTA, and production security. Python remains useful on the gateway and for hardware test fixtures.

## ESP32-S3 platform findings

ESP-IDF is Espressif's official framework. ESP-IDF 6.1 is the latest stable release at the research date; this project targets its current driver APIs. The firmware uses components so the parser remains portable and host-testable.

ESP32-S3 pins cannot be treated as interchangeable. Espressif specifically warns that GPIO26–32 are normally used by flash/PSRAM, GPIO33–37 may also be occupied with octal memory, and GPIO19/20 carry USB-JTAG by default. GPIO0, 3, 45, and 46 are strapping pins. For that reason, no generic raw-pin command exists and every profile pin defaults to disabled.

Sources: [ESP-IDF releases](https://github.com/espressif/esp-idf/releases), [ESP32-S3 GPIO restrictions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html).

ESP-IDF supports fast Linux host tests, but hardware integration tests are still required because host mocks cannot represent electrical behavior or every peripheral interaction. This repository therefore has strict-C host tests now and reserves on-target tests for the real board.

Source: [running ESP-IDF applications on host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/host-apps.html).

## Security findings

Production firmware should use HTTPS OTA with rollback validation, Secure Boot v2, flash encryption, encrypted NVS, and authenticated provisioning. Espressif recommends Secure Boot for production and describes OTA rollback as a way to mark an image active only after diagnostics pass. Those irreversible eFuse features should not be enabled on the first development board until the recovery and signing process is proven.

The TypeSafe credential must not live in distributed firmware. Extracting a shared key from one device would compromise every device and key rotation would require fleet updates. The production path is ESP32 ↔ authenticated gateway/broker ↔ TypeSafe. A direct device-to-TypeSafe mode may be added only for controlled prototypes, with a per-device provisioned key in encrypted NVS.

Sources: [ESP32-S3 security overview](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/security.html), [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/secure-boot-v2.html), [certificate bundle](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_crt_bundle.html).

## Chosen direction

Use ESP-IDF with three layers:

1. A Jev gateway classifies user text into an explicit tool, target, and bounded argument.
2. A deterministic canonical command is checked against confidence, safety, and board capability policies.
3. The ESP32 independently parses and validates the command before a narrow driver executes it.

This gives Jev the useful fuzzy decision while keeping physical authority inside ordinary, testable code.

