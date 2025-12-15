############
# Makefile #
############

APP		:= uzenet-emscripten-bridge
SRCS	:= uzenet-emscripten-bridge.c
OBJS	:= $(SRCS:.c=.o)

CC		?= gcc
STRIP	?= strip
RM		?= rm -f
MKDIR_P	?= mkdir -p
INSTALL	?= install

PREFIX	?= /usr/local
BINDIR	?= $(PREFIX)/bin

# Optional: embed a default hostmap path at compile time if you want
# CFLAGS += -DUEMB_DEFAULT_HOSTMAP="\"/etc/uzenet/uzenet-hostmap.cfg\""

CFLAGS	?=
CFLAGS	+= -std=gnu17 -O2 -pipe
CFLAGS	+= -Wall -Wextra -Wpedantic
CFLAGS	+= -D_GNU_SOURCE
CFLAGS	+= -DUEMB_STANDALONE_MAIN

LDFLAGS	?=
LDLIBS	?=

.PHONY: all clean install uninstall strip debug

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $< -o $@

strip: $(APP)
	$(STRIP) $(APP)

clean:
	$(RM) $(APP) $(OBJS)

install: $(APP)
	$(MKDIR_P) $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(APP) $(DESTDIR)$(BINDIR)/$(APP)

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(APP)

debug: CFLAGS := -std=gnu17 -O0 -g -pipe -Wall -Wextra -Wpedantic -D_GNU_SOURCE -DUEMB_STANDALONE_MAIN
debug: clean all
