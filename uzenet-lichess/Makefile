# Makefile for uzenet-lichess

PROJECT	 := uzenet-lichess
CC       ?= gcc

CFLAGS   ?= -O2 -g -Wall -Wextra -pthread
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?= -lcurl -lpthread

SRCS     := uzenet-lichess.c
HDRS     := uzenet-lichess.h

all: $(PROJECT)

$(PROJECT): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(PROJECT) *.o

.PHONY: all clean
