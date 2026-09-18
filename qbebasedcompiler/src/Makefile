CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -g
SRC    = src/main.c
BIN    = bin/testcompiler.exe

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: $(BIN)
	./$(BIN) test.c

clean:
	rm -f $(BIN)

.PHONY: all run clean
