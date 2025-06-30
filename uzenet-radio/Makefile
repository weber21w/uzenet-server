CC := gcc
PKG_CONFIG := pkg-config

# FFmpeg components (add libavutil)
FFMPEG_PKGS := libavformat libavcodec libswresample libavutil

CFLAGS   := -Wall -Wextra -O2 $(shell $(PKG_CONFIG) --cflags $(FFMPEG_PKGS))
LDFLAGS  := -pthread $(shell $(PKG_CONFIG) --libs $(FFMPEG_PKGS))

SRC      := uzenet-radio-server.c
TARGET   := uzenet-radio-server

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
