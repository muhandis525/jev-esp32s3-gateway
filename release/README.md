# Factory image

`jev-esp32s3-factory-v1.0.0.bin` is a merged, sensor-free image for ESP32-S3 boards with at least 4 MB flash. It contains the bootloader, dual-OTA partition table, initial OTA metadata, and application.

Install `esptool` (or use a system with `uvx`), connect one ESP32-S3, and run:

```sh
./release/flash-new.sh /dev/ttyACM0
```

The script verifies the SHA-256 checksum, confirms the serial path is a character device, performs a complete factory erase, and writes the image. **A factory erase permanently removes all firmware, Wi-Fi credentials, and stored data already on that board.**

After flashing, open the port at 115200 baud. Save the one-time provisioning record, then use the advertised `JEV-XXXXXX` BLE service and printed proof of possession to configure Wi-Fi. All sensor and actuator GPIOs remain disabled until a board profile is intentionally configured and rebuilt.
