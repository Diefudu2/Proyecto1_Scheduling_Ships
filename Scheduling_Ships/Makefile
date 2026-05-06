# Makefile – Scheduling Ships (CE 4303 TEC)

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I./include
LDFLAGS = -lrt

SRC     = src/scheduler.c
OBJ     = $(SRC:.c=.o)

TEST_SRC = tests/test_schedulers.c
TEST_BIN = test_schedulers

.PHONY: all test clean

all: $(TEST_BIN)

$(TEST_BIN): $(SRC) $(TEST_SRC) include/thread_lib.h
	$(CC) $(CFLAGS) $(SRC) $(TEST_SRC) -o $(TEST_BIN) $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN) src/*.o