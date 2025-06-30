# Makefile for Uzenet IRC Server (no pkg-config)

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread
LDFLAGS := -levent -levent_extra -levent_openssl -lssl -lcrypto

TARGET  := uzenet-irc-server
SRCS    := uzenet-irc-server.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: all
	sudo ./install-uzenet-irc.sh

uninstall:
	sudo ./remove-uzenet-irc.sh

clean:
	rm -f $(TARGET)
