# ============================================================
# Makefile - Scheduling Ships ESP32-C6
# ESP-IDF + FreeRTOS + C + GUI
# ============================================================

SHELL := /bin/bash

TARGET := esp32c6
PORT   ?= /dev/ttyACM0
BAUD   ?= 115200

IDF_PATH ?= /home/andre/esp/esp-idf
IDF_EXPORT := $(IDF_PATH)/export.sh

PYTHON ?= python3
PIP    ?= pip3

IDF_CMD := export IDF_PATH=$(IDF_PATH); \
           export IDF_PATH_FORCE=1; \
           source $(IDF_EXPORT) >/dev/null 2>&1; \
           idf.py

.PHONY: all run run-monitor build flash monitor target clean fullclean erase menuconfig size gui deps ports help check-idf check-port

all: build

check-idf:
	@if [ ! -f "$(IDF_EXPORT)" ]; then \
		echo "ERROR: No se encontró ESP-IDF en $(IDF_EXPORT)"; \
		echo "Use: make run IDF_PATH=/ruta/a/esp-idf"; \
		exit 1; \
	fi

check-port:
	@if [ ! -e "$(PORT)" ]; then \
		echo "ERROR: No existe el puerto $(PORT)"; \
		echo "Puertos disponibles:"; \
		ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true; \
		echo ""; \
		echo "Use: make run PORT=/dev/ttyACM0"; \
		exit 1; \
	fi

# Uso normal:
# make run = compila, flashea y abre la GUI.
run: check-idf check-port
	$(IDF_CMD) build
	$(IDF_CMD) -p $(PORT) flash
	@echo ""
	@echo "Firmware cargado correctamente."
	@echo "Abriendo GUI..."
	@echo ""
	$(PYTHON) GUI/gui.py

# Alternativa si quiere usar el monitor serial de ESP-IDF.
run-monitor: check-idf check-port
	$(IDF_CMD) build
	$(IDF_CMD) -p $(PORT) flash
	$(IDF_CMD) -p $(PORT) -b $(BAUD) monitor

build: check-idf
	$(IDF_CMD) build

flash: check-idf check-port
	$(IDF_CMD) -p $(PORT) flash

monitor: check-idf check-port
	$(IDF_CMD) -p $(PORT) -b $(BAUD) monitor

target: check-idf
	$(IDF_CMD) set-target $(TARGET)

clean: check-idf
	$(IDF_CMD) clean

fullclean: check-idf
	$(IDF_CMD) fullclean

erase: check-idf check-port
	$(IDF_CMD) -p $(PORT) erase-flash

menuconfig: check-idf
	$(IDF_CMD) menuconfig

size: check-idf
	$(IDF_CMD) size

gui:
	$(PYTHON) GUI/gui.py

deps:
	$(PIP) install pyserial

ports:
	@ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "No se detectaron puertos."

help:
	@echo "Comandos:"
	@echo "  make run          Compila, flashea y abre la GUI"
	@echo "  make run-monitor  Compila, flashea y abre monitor serial ESP-IDF"
	@echo "  make build        Solo compila"
	@echo "  make flash        Solo flashea"
	@echo "  make monitor      Solo abre monitor serial"
	@echo "  make gui          Solo abre la GUI"
	@echo "  make deps         Instala pyserial para la GUI"
	@echo "  make ports        Lista puertos disponibles"
	@echo ""
	@echo "Ejemplos:"
	@echo "  make run"
	@echo "  make run PORT=/dev/ttyACM0"
	@echo "  make run-monitor"
	@echo "  make gui"