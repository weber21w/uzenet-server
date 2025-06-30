# Makefile for uzenet-room-server

CC     := gcc
CFLAGS := -O2 -Wall -pthread
TARGET := uzenet-room-server

IDENTITY := ../uzenet-identity/uzenet-identity-client.c

.PHONY: all clean install remove status

all: $(TARGET)

$(TARGET): uzenet-room-server.c $(IDENTITY)
	$(CC) $(CFLAGS) -o $@ uzenet-room-server.c $(IDENTITY)

install: all
	@echo "→ Invoking install script"
	@chmod +x install-uzenet-room.sh
	@sudo ./install-uzenet-room.sh

remove:
	@echo "→ Invoking remove script"
	@chmod +x remove-uzenet-room.sh
	@sudo ./remove-uzenet-room.sh

status:
	@echo "→ Invoking status script"
	@chmod +x status-uzenet-room.sh
	@./status-uzenet-room.sh

clean:
	@echo "→ Cleaning up"
	@rm -f $(TARGET)
