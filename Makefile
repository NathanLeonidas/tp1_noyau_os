CC = gcc
CFLAGS = -Wall -Wextra -O0 -Iutils
LDFLAGS = -pthread

BIN_DIR = bin
UTILS = utils/timer.c

MOTS ?= utils/mots.txt
LOG1 ?= utils/logs.txt
LOG2 ?= utils/logs_2.txt
N ?= 3

.PHONY: all run1 run2 clean

all: $(BIN_DIR)/section1 $(BIN_DIR)/section2

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/section1: section1/section1.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/section2: section2/section2.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run1: $(BIN_DIR)/section1
	./$(BIN_DIR)/section1 $(MOTS)

run2: $(BIN_DIR)/section2
	./$(BIN_DIR)/section2 $(LOG1) $(LOG2) $(N)

clean:
	rm -rf $(BIN_DIR)
