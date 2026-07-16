CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LIB     = ../pixel-lights
B       = build

.PHONY: all clean FORCE

all: $(B)/claude-light

$(B):
	mkdir -p $(B)

$(LIB)/build/libpixel_lights.a: FORCE
	$(MAKE) -C $(LIB)

FORCE:

$(B)/claude-light: src/claude_light.c $(LIB)/build/libpixel_lights.a | $(B)
	$(CC) $(CFLAGS) -I$(LIB)/src src/claude_light.c \
		$(LIB)/build/libpixel_lights.a -lm -o $@

clean:
	rm -rf $(B)
