# Makefile for uzenet-video server

TARGET = uzenet-video
SRC    = uzenet-video.c
CFLAGS = -Wall -O2 -pthread
LDFLAGS = -lavformat -lavcodec -lavutil -lswscale -lswresample -lm -pthread

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
