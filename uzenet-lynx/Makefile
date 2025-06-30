# Makefile for Uzenet Lynx Server

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread
LDFLAGS := -lutil               # forkpty() lives here
TARGET  := uzenet-lynx-server
SRCS    := uzenet-lynx-server.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: all
	@echo "[INSTALL] Running install-uzenet-lynx.sh"
	sudo ./install-uzenet-lynx.sh

uninstall:
	@echo "[UNINSTALL] Running remove-uzenet-lynx.sh"
	sudo ./remove-uzenet-lynx.sh

clean:
	rm -f $(TARGET)
