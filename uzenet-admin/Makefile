# Makefile for uzenet-admin-server with embedded cmark

CC     := gcc
CFLAGS := -O2 -Wall
LDLIBS := -lmicrohttpd -lcrypt
TARGET := uzenet-admin-server

SRCS   := uzenet-admin-server.c cmark_embedded.c

.PHONY: all clean install remove status

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

install: all
	@echo "→ Invoking install script"
	@chmod +x install-uzenet-admin.sh
	@sudo ./install-uzenet-admin.sh

remove:
	@echo "→ Invoking remove script"
	@chmod +x remove-uzenet-admin.sh
	@sudo ./remove-uzenet-admin.sh

status:
	@echo "→ Invoking status script"
	@chmod +x status-uzenet-admin.sh
	@./status-uzenet-admin.sh

clean:
	@echo "→ Cleaning up"
	@rm -f $(TARGET)
