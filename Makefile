CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
CPPFLAGS += -Iinclude
LDFLAGS ?=

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
BIN := translator.exe
LDLIBS += -lwinhttp -lws2_32
else
BIN := translator
LDLIBS += -lcurl
endif

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) translator translator.exe

.PHONY: all clean
