# Makefile for uzenet-virtual-fujinet

CC      ?= gcc
CFLAGS  ?= -Wall -O2 -g
LDFLAGS ?= -lpthread

TARGET  = uzenet-virtual-fujinet

SRCS = \
	uzenet-virtual-fujinet.c \
	../uzenet-tunnel/uzenet-tunnel.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
