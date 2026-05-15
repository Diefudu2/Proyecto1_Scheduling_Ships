# Makefile – Scheduling Ships
# CE 4303 – Principios de Sistemas Operativos

CC      = gcc
CFLAGS  = -Wall -Wextra -g -O0 -D_XOPEN_SOURCE=700 -Iinclude
LDFLAGS = -pthread -lncurses

SRC_DIR = src
BUILD   = build

SRCS = $(SRC_DIR)/ship.c \
       $(SRC_DIR)/canal.c \
       $(SRC_DIR)/gui.c \
       $(SRC_DIR)/hardware_serial.c \
       $(SRC_DIR)/hardware_leds.c \
       $(SRC_DIR)/scheduler.c \
       $(SRC_DIR)/uthread.c \
       $(SRC_DIR)/main.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD)/%.o, $(SRCS))

TARGET = $(BUILD)/ships

.PHONY: all clean run install tests test_uthread test_scheduler test_rr_quantum

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

run: all
	./$(TARGET)

test_uthread: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_uthread tests/test_uthread.c $(SRC_DIR)/uthread.c
	./$(BUILD)/test_uthread

test_scheduler: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_scheduler tests/test_scheduler.c $(SRC_DIR)/scheduler.c $(SRC_DIR)/uthread.c $(LDFLAGS)
	./$(BUILD)/test_scheduler

test_rr_quantum: $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/test_rr_quantum tests/test_rr_quantum.c $(SRC_DIR)/scheduler.c $(SRC_DIR)/uthread.c $(LDFLAGS)
	./$(BUILD)/test_rr_quantum

tests: test_uthread test_scheduler test_rr_quantum

install: all
	cp $(TARGET) /usr/local/bin/ships

clean:
	rm -rf $(BUILD)