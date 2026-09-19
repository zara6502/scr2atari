CC = gcc
CFLAGS = -O3 -Wall -Wextra
TARGET = scr2atari.exe

SOURCES = \
    scr2atari.c \
    scr.c \
    png.c \
    qoi.c \
    atari.c \
    xex.c \
    files.c \
    filters.c

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean