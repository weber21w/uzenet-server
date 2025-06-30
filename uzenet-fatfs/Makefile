# Makefile for Uzenet FatFS Server

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread
TARGET  := uzenet-fatfs-server
SRCS    := uzenet-fatfs-server.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	@echo "[INSTALL] Running install-uzenet-fatfs.sh"
	sudo ./install-uzenet-fatfs.sh

uninstall:
	@echo "[UNINSTALL] Running remove-uzenet-fatfs.sh"
	sudo ./remove-uzenet-fatfs.sh

clean:
	rm -f $(TARGET)
