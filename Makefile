CC ?= gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -O3 -g3
LFLAGS = -lpng
TARGET = evaluate
TEST_TARGET = tests/test_bfg

SRC = bfg.c png_convert.c evaluate.c
HEADERS = bfg.h convert.h util.h
OBJ = $(SRC:%.c=%.o)

all: $(TARGET)

$(TARGET): $(OBJ) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Synthetic unit tests (no libpng needed)
$(TEST_TARGET): tests/test_bfg.c bfg.c $(HEADERS)
	$(CC) $(CFLAGS) -I. -o $(TEST_TARGET) tests/test_bfg.c bfg.c

test: $(TEST_TARGET)
	./$(TEST_TARGET)

# Run benchmark on a subset of images
bench: $(TARGET)
	@echo "=== photo_kodak ===" && ./$(TARGET) images/photo_kodak/*.png
	@echo ""
	@echo "=== textures_photo ===" && ./$(TARGET) images/textures_photo/*.png
	@echo ""
	@echo "=== screenshot_web ===" && ./$(TARGET) images/screenshot_web/*.png
	@echo ""
	@echo "=== icon_64 ===" && ./$(TARGET) images/icon_64/*.png

# Full benchmark on all image categories
bench-all: $(TARGET)
	@for dir in images/*/; do \
		name=$$(basename "$$dir"); \
		pngs=$$(find "$$dir" -name '*.png' | head -50); \
		if [ -n "$$pngs" ]; then \
			echo "=== $$name ==="; \
			echo $$pngs | xargs ./$(TARGET); \
			echo ""; \
		fi; \
	done

.PHONY: clean test bench bench-all
clean:
	$(RM) -r $(TARGET) $(TEST_TARGET) $(OBJ) $(TARGET).dSYM vgcore.* output/
