# Makefile – Scheduling Ships
# CE 4303 – Principios de Sistemas Operativos

CC      = gcc
CFLAGS  = -Wall -Wextra -g -O0 -Iinclude
LDFLAGS = -lpthread -lncurses

SRC_DIR  = src
BUILD    = build

SRCS = $(SRC_DIR)/ship.c \
       $(SRC_DIR)/canal.c \
       $(SRC_DIR)/gui.c \
       $(SRC_DIR)/hardware_serial.c \
       $(SRC_DIR)/hardware_leds.c \
       $(SRC_DIR)/main.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD)/%.o, $(SRCS))

TARGET = $(BUILD)/ships

.PHONY: all clean install

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo ""
	@echo "  Compilacion exitosa -> $(TARGET)"
	@echo "  Ejecutar: ./$(TARGET)"
	@echo ""

install: all
	cp $(TARGET) /usr/local/bin/ships

clean:
	rm -rf $(BUILD)