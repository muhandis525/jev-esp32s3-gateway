# Connectivity guide

## First boot: provision Wi-Fi over BLE

1. Connect the ESP32-S3 USB Serial/JTAG port and open the ESP-IDF monitor.
2. Record the one-line provisioning data printed at boot: BLE service name, proof of possession, device ID, and API token.
3. Use Espressif's provisioning application or `esp_prov` with Security 1, the printed proof, and the advertised `JEV-XXXXXX` device.
4. Send the Wi-Fi SSID and password. The device stores them in NVS and restarts.
5. After association, the serial console prints the acquired IPv4 address as JSON.

The TypeSafe API key is never sent to or stored on the device. The proof and API token are unique random values generated on that device.

## Wi-Fi API

The health endpoint is intentionally unauthenticated and contains no device details:

```sh
curl -s http://DEVICE_IP/healthz
```

Status and execution require the per-device bearer token:

```sh
curl -s http://DEVICE_IP/api/v1/status \
  -H "Authorization: Bearer $JEV_DEVICE_TOKEN"

curl -s http://DEVICE_IP/api/v1/command \
  -H "Authorization: Bearer $JEV_DEVICE_TOKEN" \
  -H 'content-type: text/plain' \
  --data 'system info'
```

Request bodies are canonical commands, limited to 127 bytes. Responses are JSON. The command engine is serialized, so Wi-Fi, BLE, and USB clients cannot race peripheral operations.

## BLE local control

After provisioning, the device exposes a separate NimBLE GATT control service. Pair with encryption enabled, then write this UTF-8 payload to the command characteristic:

```text
32-character-device-token|system info
```

Read or subscribe to the response characteristic for the JSON result. The service supports one connection to reduce memory use on the minimal module. The token is still required even on an encrypted BLE link.

## Recovery

- USB remains available for local canonical commands.
- `network reset` erases only provisioned Wi-Fi state and restarts BLE provisioning.
- `stop` immediately drives configured digital outputs low and PWM duty to zero.
- If Wi-Fi disappears, local BLE and USB control remain available.

The current REST interface is plain HTTP. Keep it on an isolated or trusted LAN; do not port-forward it. MQTT or HTTPS with TLS, replay protection, command IDs, and expiry is the production upgrade path.
