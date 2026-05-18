# ============================================================
# Makefile - Scheduling Ships ESP32-C6
# ESP-IDF + FreeRTOS + C
# ============================================================

SHELL := /bin/bash

PROJECT_NAME := SchedulingShips
TARGET       := esp32c6

# Puerto serial por defecto
PORT         ?= /dev/ttyACM0
BAUD         ?= 115200

# Ruta de ESP-IDF
IDF_PATH ?= /home/andre/esp/esp-idf
IDF_EXPORT := $(IDF_PATH)/export.sh

# Forzar IDF_PATH para que export.sh funcione desde make
IDF_CMD := export IDF_PATH=$(IDF_PATH); export IDF_PATH_FORCE=1; source $(IDF_EXPORT) >/dev/null 2>&1; idf.py

.PHONY: all check-idf target build flash monitor run clean fullclean menuconfig erase size gui add-ledstrip help

all: build

check-idf:
	@if [ ! -f "$(IDF_EXPORT)" ]; then \
		echo "ERROR: No se encontró ESP-IDF en:"; \
		echo "  $(IDF_EXPORT)"; \
		echo ""; \
		echo "Si ESP-IDF está en otra ruta, use:"; \
		echo "  make run IDF_PATH=/ruta/a/esp-idf PORT=/dev/ttyACM0"; \
		exit 1; \
	fi

target: check-idf
	$(IDF_CMD) set-target $(TARGET)

build: check-idf
	$(IDF_CMD) build

flash: check-idf
	$(IDF_CMD) -p $(PORT) flash

monitor: check-idf
	$(IDF_CMD) -p $(PORT) -b $(BAUD) monitor

run: target
	$(IDF_CMD) build
	$(IDF_CMD) -p $(PORT) flash
	$(IDF_CMD) -p $(PORT) -b $(BAUD) monitor

clean: check-idf
	$(IDF_CMD) clean

fullclean: check-idf
	$(IDF_CMD) fullclean

menuconfig: check-idf
	$(IDF_CMD) menuconfig

erase: check-idf
	$(IDF_CMD) -p $(PORT) erase-flash

size: check-idf
	$(IDF_CMD) size

add-ledstrip: check-idf
	$(IDF_CMD) add-dependency "espressif/led_strip" --path Ships

gui:
	python3 GUI/gui.py

help:
	@echo "Comandos disponibles:"
	@echo "  make target                  Configura el target como esp32c6"
	@echo "  make build                   Compila el proyecto"
	@echo "  make flash                   Carga el firmware en $(PORT)"
	@echo "  make monitor                 Abre monitor serial en $(PORT)"
	@echo "  make run                     Compila, flashea y abre monitor"
	@echo "  make clean                   Limpieza normal"
	@echo "  make fullclean               Limpieza completa"
	@echo "  make menuconfig              Abre configuración ESP-IDF"
	@echo "  make erase                   Borra la flash"
	@echo "  make size                    Muestra tamaño del firmware"
	@echo "  make add-ledstrip            Agrega dependencia led_strip al componente Ships"
	@echo "  make gui                     Ejecuta interfaz Python"
	@echo ""
	@echo "Variables:"
	@echo "  PORT=/dev/ttyACM0"
	@echo "  PORT=/dev/ttyUSB0"
	@echo "  BAUD=115200"
	@echo "  IDF_PATH=/home/andre/esp/esp-idf"
	@echo ""
	@echo "Ejemplos:"
	@echo "  make run"
	@echo "  make run PORT=/dev/ttyACM0"
	@echo "  make flash PORT=/dev/ttyACM0"