PORT ?= /dev/ttyACM0

.PHONY: test test-c test-python configure build flash monitor factory

test: test-c test-python

test-c:
	$(MAKE) -C tests/host test

test-python:
	python3 -m unittest discover -s tests/gateway -v

configure:
	idf.py set-target esp32s3

build:
	idf.py build

flash: build
	idf.py -p $(PORT) flash

monitor:
	idf.py -p $(PORT) monitor

factory: build
	mkdir -p release
	python -m esptool --chip esp32s3 merge-bin \
		--output release/jev-esp32s3-factory-v1.0.0.bin \
		--flash-mode dio --flash-freq 80m --flash-size 4MB \
		0x0 build/bootloader/bootloader.bin \
		0x8000 build/partition_table/partition-table.bin \
		0xf000 build/ota_data_initial.bin \
		0x20000 build/jev_esp32s3.bin
	cd release && sha256sum jev-esp32s3-factory-v1.0.0.bin > jev-esp32s3-factory-v1.0.0.bin.sha256
