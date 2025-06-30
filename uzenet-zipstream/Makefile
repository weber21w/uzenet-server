# Makefile for Uzenet Zipstream Server

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread
TARGET  := uzenet-zipstream-server
SRCS    := uzenet-zipstream-server.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	@echo "[INSTALL] Running install-uzenet-zipstream.sh"
	sudo ./install-uzenet-zipstream.sh

uninstall:
	@echo "[UNINSTALL] Running remove-uzenet-zipstream.sh"
	sudo ./remove-uzenet-zipstream.sh

clean:
	rm -f $(TARGET)
