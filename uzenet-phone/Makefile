# Makefile for Uzenet Phone Server
CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -pthread \
           $(shell pkg-config --cflags libpj libpjlib-util libpjnath libpjmedia libpjsua2)
LDFLAGS := $(shell pkg-config --libs   libpj libpjlib-util libpjnath libpjmedia libpjsua2) \
           -lspeexdsp -lm

TARGET  := uzenet-phone-server
SRCS    := uzenet-phone-server.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: all
	sudo ./install-uzenet-phone.sh

uninstall:
	sudo ./remove-uzenet-phone.sh

clean:
	rm -f $(TARGET)
