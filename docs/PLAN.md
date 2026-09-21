# Implementation plan

## Phase 0 — research and safety model (complete)

- Compared Jev's official smart-home pattern, ESPHome, Tasmota/Berry, Firmata, and MicroPython.
- Selected ESP-IDF + host Jev gateway + deterministic on-device DSL.
- Kept the API credential out of source and firmware.
- Defined trust boundaries and fail-closed behavior.

## Phase 1 — testable firmware foundation (complete in source)

- ESP-IDF project and 4 MB dual-OTA partition table.
- Allocation-free command parser with strict grammar and bounds.
- Compile-time semantic board profile; all pins disabled by default.
- GPIO input/output, ADC raw read, PWM, servo, I2C scan, system info, and stop tools.
- USB serial command loop with JSON-line responses.
- Strict-C parser tests.
- Jev gateway with batched questions, confidence/safety gates, and unit tests.
- ESP32-S3-MINI-1 conservative GPIO policy and duplicate-pin rejection.
- Per-device provisioning proof and API token persisted in NVS.
- Secure BLE Wi-Fi provisioning, normal-mode BLE control, and Wi-Fi REST control.
- Gateway-to-device authenticated command forwarding.

The host test suite and ESP-IDF 6.1 target build pass. Factory erase/flash and boot tests pass on a connected ESP32-S3 rev 0.2 with 16 MB flash and 8 MB embedded PSRAM. The sensor-free image advertises BLE provisioning, accepts complete UART commands without fragmentation, and rejects actuator commands because all GPIO assignments remain disabled.

## Phase 2 — first hardware bring-up

Required facts from the board owner:

1. Exact board/module name and a pinout link or schematic.
2. Exact development-board name or schematic; the chip probe reports an N16R8-class configuration.
3. Every sensor: exact part number, bus, address, voltage, and desired units/rate.
4. Every actuator: exact part/driver, control signal, supply voltage/current, safe state, and maximum run time.
5. Whether the final production module is MINI-1-N4, N8, or N4R2; the connected development board is larger.

Work:

- Rebuild with the selected production-board pin profile after hardware is chosen.
- Create a named board profile and reject reserved/conflicting pins at configuration time.
- Flash, verify boot and serial console, then test each input before attaching actuators.
- Add calibrated ADC values and concrete sensor drivers through managed components where appropriate.
- Verify watchdog recovery, brownout behavior, and output state through reset/bootloader transitions.

Exit criteria: a recorded test matrix proves every command, invalid command, range boundary, disconnect, and reset produces the expected safe result on the actual hardware.

## Phase 3 — gateway-to-device transport (prototype complete)

- BLE Wi-Fi provisioning is implemented; time synchronization remains.
- Authenticated REST command/status transport is implemented for a trusted LAN.
- MQTT/HTTPS over TLS remains the production transport.
- Define a versioned production envelope with device ID, command ID, issued time, expiry, and canonical command.
- Add replay prevention and idempotent acknowledgements.
- Do not forward a gateway result whose status is not `ok`.

Exit criteria: integration tests cover duplicate, expired, reordered, malformed, and unauthorized messages as well as offline recovery.

## Phase 4 — Jev evaluation and tuning

- Build a labeled corpus containing normal, ambiguous, compound, adversarial, multilingual, and typo-heavy requests.
- Record full distributions, confidence, model version, selected command, rejection reason, and eventual result without storing secrets.
- Tune thresholds per tool risk. Reads may allow a lower threshold than motor/relay writes.
- Pin the model version. Test any new Jev version offline before promotion.
- Add explicit confirmation for high-impact actuators and maximum-duration interlocks in firmware.

Exit criteria: measured false-execution and false-rejection rates meet an agreed risk budget for each tool.

## Phase 5 — production hardening

- HTTPS OTA with first-boot diagnostics and automatic rollback.
- Secure Boot v2, flash encryption, and encrypted NVS after the provisioning/recovery process is proven on sacrificial hardware.
- Per-device credentials and rotation/revocation process.
- Hardware watchdog/interlock for hazardous actuators; software stop is not sufficient.
- Structured telemetry, crash dumps, fleet version inventory, and staged rollout.
- Fuzz parser and command-envelope decoders; run static analysis and on-target stress tests.

Exit criteria: manufacturing provisioning, recovery, signed release, incident response, and key-rotation procedures are documented and rehearsed.
