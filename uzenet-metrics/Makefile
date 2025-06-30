# Makefile for uzenet-metrics

CC     := gcc
CFLAGS := -O2 -Wall
# add -lpthread so the pthread symbols resolve
LDLIBS := -lmicrohttpd -lpthread
TARGET := uzenet-metrics-server

.PHONY: all clean install remove status

all: $(TARGET)

$(TARGET): uzenet-metrics-server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: all
	@echo "→ Invoking install script"
	@chmod +x install-uzenet-metrics.sh
	@sudo ./install-uzenet-metrics.sh

remove:
	@echo "→ Invoking remove script"
	@chmod +x remove-uzenet-metrics.sh
	@sudo ./remove-uzenet-metrics.sh

status:
	@echo "→ Invoking status script"
	@chmod +x status-uzenet-metrics.sh
	@./status-uzenet-metrics.sh

clean:
	@echo "→ Cleaning up"
	@rm -f $(TARGET)
