#!/usr/bin/env sh
set -eu

release_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
port=${1:-/dev/ttyACM0}
image="$release_dir/jev-esp32s3-factory-v1.0.0.bin"
checksum="$image.sha256"

if [ ! -c "$port" ]; then
    echo "Serial port is not a character device: $port" >&2
    exit 2
fi
if [ ! -f "$image" ] || [ ! -f "$checksum" ]; then
    echo "Factory image or checksum is missing in $release_dir" >&2
    exit 2
fi
(cd "$release_dir" && sha256sum -c "$(basename "$checksum")")

if command -v esptool >/dev/null 2>&1; then
    run_esptool() { esptool "$@"; }
elif python3 -c 'import esptool' >/dev/null 2>&1; then
    run_esptool() { python3 -m esptool "$@"; }
elif command -v uvx >/dev/null 2>&1; then
    run_esptool() { uvx --from esptool esptool "$@"; }
else
    echo "Install esptool first: python3 -m pip install esptool" >&2
    exit 2
fi

echo "Factory-erasing and flashing ESP32-S3 on $port"
# Intentional factory operation: removes all previous firmware and credentials.
run_esptool --chip esp32s3 --port "$port" erase-flash
run_esptool --chip esp32s3 --port "$port" --baud 460800 \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0 "$image"
echo "Factory image installed. Open a 115200-baud monitor for provisioning data."
