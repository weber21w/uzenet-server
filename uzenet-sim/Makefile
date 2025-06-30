# Makefile for uzenet-sim

CC     := gcc
CFLAGS := -O2 -Wall
TARGET := uzenet-sim-server

.PHONY: all clean install remove status

all: $(TARGET)

$(TARGET): uzenet-sim-server.c
	$(CC) $(CFLAGS) -o $@ $<

install: all
	@echo "→ Invoking install script"
	@chmod +x install-uzenet-sim.sh
	@sudo ./install-uzenet-sim.sh

remove:
	@echo "→ Invoking remove script"
	@chmod +x remove-uzenet-sim.sh
	@sudo ./remove-uzenet-sim.sh

status:
	@echo "→ Invoking status script"
	@chmod +x status-uzenet-sim.sh
	@./status-uzenet-sim.sh

clean:
	@echo "→ Cleaning up"
	@rm -f $(TARGET)
