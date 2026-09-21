# Jev ESP32-S3 firmware

[![Tests](https://github.com/muhandis525/jev-esp32s3-gateway/actions/workflows/test.yml/badge.svg)](https://github.com/muhandis525/jev-esp32s3-gateway/actions/workflows/test.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

Turn a plain-language request into a safe, authenticated ESP32-S3 action.
Jev combines BLE onboarding, Wi-Fi control, a strict command language, and
hardware safety gates in a small open-source gateway.

**Try first:** run `make test`, then read the [factory flashing guide](release/README.md).

Connected ESP-IDF firmware for an ESP32-S3-MINI-1-class module, plus a host gateway that uses TypeSafe Jev to translate short natural-language requests into safe sensor and actuator commands. The baseline is 4 MB flash with no PSRAM requirement. It also runs on larger ESP32-S3 variants.

The project intentionally keeps intelligence and authority separate:

```text
user text -> Jev gateway -> validated canonical command -> authenticated Wi-Fi API
                                                        -> ESP32 parser -> pin policy -> driver
                         \-> reject / ask for clarification
```

Jev never writes a pin directly. The firmware accepts only configured target names and bounded values. No raw GPIO number, arbitrary memory access, shell, or firmware-write command exists.

## Current tools

| Command | Configured target | Effect |
| --- | --- | --- |
| `digital read button` | digital input | Read 0/1 |
| `digital write led on` | digital output | Set 0/1 |
| `digital write relay off` | digital output | Set 0/1 |
| `analog read analog` | ADC input | Read raw ADC value |
| `pwm set fan 73` | PWM output | Set duty 0–100% |
| `servo set servo 90` | servo output | Set 0–180 degrees |
| `i2c scan` | configured bus | List responding 7-bit addresses |
| `system info` | system | Chip/flash information |
| `network reset` | Wi-Fi | Erase Wi-Fi credentials and return to BLE provisioning |
| `stop` | all outputs | Relay/LED low and PWM zero |

All pins are disabled by default. The minimal profile also rejects boot-strap pins, USB-console pins, nonexistent GPIOs, flash/PSRAM pins, and duplicate assignments.

## Connectivity

- **BLE provisioning:** a fresh device advertises as `JEV-XXXXXX`. Espressif Network Provisioning uses X25519, proof-of-possession authentication, and AES-CTR to deliver Wi-Fi credentials.
- **BLE local control:** after provisioning and reboot, an encrypted NimBLE GATT service accepts `api-token|canonical command` and provides a readable/notifiable JSON response characteristic.
- **Wi-Fi control:** authenticated REST endpoints expose health, device status, and command execution.
- **USB:** the same command engine remains available over USB Serial/JTAG for recovery.

Every device generates its own provisioning proof and 128-bit API token in NVS. The TypeSafe key stays on the gateway and is never installed on the ESP32.

## Test what works now

The parser and gateway safety policy run without ESP-IDF or hardware:

```sh
make test
```

## Configure and build the firmware

Use ESP-IDF 6.1, activate its environment, then:

```sh
source /home/muhammad/.espressif/frameworks/esp-idf-v6.1/export.sh
make configure
idf.py menuconfig
make build
make flash PORT=/dev/ttyACM0
make monitor PORT=/dev/ttyACM0
```

In `Jev minimal ESP32-S3-MINI-1 profile`, assign the pins wired on the actual board. The default partition table supports 4 MB or larger flash and has two OTA application slots of 1.8125 MB each.

On the first boot, the USB monitor prints one provisioning record containing the BLE name, proof-of-possession, and device API token. Save the token, then provision using Espressif's provisioning app or the `esp_prov` tool included with `espressif/network_provisioning`. The device restarts automatically after joining Wi-Fi and prints its IP address.

The firmware deliberately does not require or allocate PSRAM and remains compatible with the minimal 4 MB profile. Your board's serial device may differ from `/dev/ttyACM0`; pass the correct port through `PORT` when flashing or monitoring.

The v1.0.0 target build and factory deployment were verified on that board. The application occupies 1,049,856 bytes and leaves about 45% free in each 1,856 KiB OTA slot. The tested merged image and guarded factory flasher are in [`release/`](release/). `flash-new.sh` performs a complete erase and is intended only for installing a new or repurposed ESP32-S3.

## Device API

```sh
export DEVICE_IP='192.168.1.50'
export JEV_DEVICE_TOKEN='token-from-provisioning'

curl -s "http://$DEVICE_IP/healthz"
curl -s "http://$DEVICE_IP/api/v1/status" \
  -H "Authorization: Bearer $JEV_DEVICE_TOKEN"
curl -s "http://$DEVICE_IP/api/v1/command" \
  -H "Authorization: Bearer $JEV_DEVICE_TOKEN" \
  -H 'content-type: text/plain' \
  --data 'digital write relay off'
```

The local REST transport is authenticated but currently plain HTTP, so use it only on a trusted LAN. A production deployment should place devices on an isolated IoT network or replace it with MQTT/HTTPS over TLS.

## Run the Jev gateway

No TypeSafe credential is included in this repository. Keep any local `jev.txt`
file ignored by Git and never place it in a firmware image. Environment
variables are preferred:

```sh
export TYPESAFE_API_KEY='...'
python3 gateway/jev_gateway.py --profile gateway/profile.example.json
```

If you keep the credential in a local file (not committed):

```sh
python3 gateway/jev_gateway.py --key-file jev.txt --profile gateway/profile.example.json
```

The server listens only on `127.0.0.1:8787` by default. Try:

```sh
curl -s http://127.0.0.1:8787/v1/interpret \
  -H 'content-type: application/json' \
  -d '{"text":"turn the relay off"}'
```

To interpret and immediately execute on the ESP32:

```sh
export JEV_DEVICE_TOKEN='token-from-provisioning'
python3 gateway/jev_gateway.py \
  --key-file jev.txt \
  --profile gateway/profile.example.json \
  --device-url http://192.168.1.50
```

A successful result contains the validated command and the device's execution response:

```json
{"status":"ok","command":"digital write relay off","tool":"digital_write","model":"jev-1.13.0","device":{"ok":true,"tool":"digital.write","target":"relay","value":0}}
```

## Before connecting actuators

- Do not power motors, pumps, solenoids, relays, or servos from an ESP32 GPIO.
- Use an appropriate driver, flyback protection for inductive loads, a common ground where appropriate, and a correctly sized external supply.
- Make the hardware default safe with pull resistors and normally-off driver design; software is not a safety system.
- Do not expose the ESP32 HTTP server to the Internet. Production still needs TLS, replay protection, command expiry, and stronger device identity.
- Rotate the pasted API credential if it has ever been shared outside this private workspace.

See the [connectivity guide](docs/CONNECTIVITY.md), [research and decisions](docs/RESEARCH.md), [architecture](docs/ARCHITECTURE.md), and [implementation plan](docs/PLAN.md).

## License

This project is licensed under the [Apache License 2.0](LICENSE).
