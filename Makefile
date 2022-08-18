CC ?= gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -O3 -g3
LFLAGS = -lpng
TARGET = evaluate
SRC = $(wildcard *.c)
OBJ = $(SRC:%.c=%.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LFLAGS)

.PHONY: clean
clean:
	$(RM) -r $(TARGET) $(OBJ) $(TARGET).dSYM vgcore.*
