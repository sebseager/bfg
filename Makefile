CC ?= gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -O3 -g3
LFLAGS = -lpng
TARGET = bfg

$(TARGET): bfg.c bfg.h
	$(CC) $(CFLAGS) bfg.c -o $(TARGET) $(LFLAGS)

.PHONY: clean
clean:
	$(RM) -r $(TARGET) $(TARGET).dSYM
