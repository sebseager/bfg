CC ?= gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -O3 -g3
LFLAGS = -lpng
SOURCES = test.c bfg.c png_convert.c
HEADERS = bfg.h convert.h util.h
TARGET = test

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $@ -o $(TARGET) $(LFLAGS)

.PHONY: clean
clean:
	$(RM) -r $(TARGET) $(TARGET).dSYM vgcore.*
