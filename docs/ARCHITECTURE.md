# Architecture

## Trust boundaries

```text
Untrusted user text
        |
        v
+-------------------------+
| Gateway                 |
| - TypeSafe/Jev request  |
| - confidence thresholds |
| - compound/unsafe gates |
| - device profile        |
+------------+------------+
             | canonical command + metadata
             | (later: TLS, device auth, nonce, expiry)
             v
+-------------------------+
| Authenticated transport |
| - Wi-Fi REST bearer     |
| - encrypted BLE + token |
| - local USB serial      |
+------------+------------+
             |
             v
+-------------------------+
| ESP32 command boundary  |
| - fixed grammar         |
| - length/range checks   |
| - semantic target names |
+------------+------------+
             |
             v
+-------------------------+
| Board policy and tools  |
| - target -> GPIO map    |
| - capability check      |
| - GPIO/ADC/PWM/I2C      |
| - fail-safe stop        |
+------------+------------+
             |
             v
       Physical hardware
```

No layer trusts the previous layer's decision. A valid Jev answer can still be rejected by gateway policy; a valid gateway command can still be rejected by the firmware parser or board profile.

## Command grammar

```ebnf
command       = "help"
              | "system info"
              | "stop"
              | "digital read" target
              | "digital write" target switch
              | "analog read" target
              | "pwm set" target percent
              | "servo set" target degrees
              | "i2c scan"
              | "network reset" ;

target        = 1*15(ALPHA | DIGIT | "_" | "-") ;
switch        = "on" | "off" | "high" | "low" | "1" | "0" ;
percent       = integer in [0, 100] ;
degrees       = integer in [0, 180] ;
```

The parser is allocation-free, case-insensitive, limited to 127 input bytes, and rejects extra tokens. Target syntax validity is separate from authorization: `digital write pump on` can parse, but execution fails unless `pump` exists as a digital output in the compiled board profile.

## Jev decision shape

One API call asks seven independent questions:

- `request_kind` Choice: device command, device question, conversation, unsupported.
- `tool` Choice: one tool or reject.
- `target` Choice: configured semantic target or none.
- `switch_on` Noul: requested binary final state.
- `level` Score: ten ordered levels for PWM/servo.
- `compound` Noul: more than one requested action.
- `unsafe` Noul: potential bypass, damage, or physical danger.

The policy currently requires Choice confidence ≥ 0.72, compound probability ≤ 0.35, and unsafe probability ≤ 0.20. These are starting values, not scientifically calibrated thresholds. They must be tuned from a labeled command dataset before deployment.

## Failure behavior

- Network/API failure: execute nothing.
- Low confidence or unclear argument: ask for clarification; execute nothing.
- Compound request: ask for one action at a time.
- Unknown or capability-mismatched target: reject.
- ESP reboot: configured digital outputs initialize low; PWM is not started.
- `stop`: digital outputs low and PWM duty zero. Servo power isolation is a hardware responsibility in this version.
- Gateway unavailable: canonical serial commands still work locally for maintenance.
- Access point unavailable: authenticated BLE control still works locally.
- Lost Wi-Fi credentials: `network reset` returns the device to BLE provisioning.

## Implemented transports

- BLE provisioning through Espressif `network_provisioning` Security 1.
- Normal-mode NimBLE GATT with encrypted command/response characteristics and an application token.
- Authenticated local REST API over Wi-Fi.
- USB Serial/JTAG console.

The provisioning and normal BLE services run on separate boots: provisioning success saves credentials and restarts, preventing two GATT owners from fighting over the NimBLE host.

## Production transport plan

The local HTTP API is for a trusted LAN and first hardware integration. Production should use MQTT over TLS or authenticated HTTPS with:

- unique device identity;
- monotonic command ID or nonce and replay cache;
- issued-at and short expiry timestamps;
- canonical serialization and message authentication/signature;
- acknowledgement carrying command ID, result, and firmware version;
- broker-side per-device topic authorization;
- no inbound Internet port on the ESP32.
