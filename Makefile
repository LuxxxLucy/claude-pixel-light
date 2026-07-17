CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PREFIX ?= /usr/local
B       = build

# Where to find the pixel-lights library.
#   default            link the installed library: -lpixel_lights under PREFIX.
#   PIXEL_LIGHTS=<dir> build and link that source tree instead (any location).
PIXEL_LIGHTS ?=

ifeq ($(strip $(PIXEL_LIGHTS)),)
  PL_CFLAGS = -I$(PREFIX)/include
  PL_LINK   = -L$(PREFIX)/lib -lpixel_lights
  PL_DEP    =
else
  PL_CFLAGS = -I$(PIXEL_LIGHTS)/src
  PL_LINK   = $(PIXEL_LIGHTS)/build/libpixel_lights.a
  PL_DEP    = $(PIXEL_LIGHTS)/build/libpixel_lights.a
endif

.PHONY: all clean

all: $(B)/claude-light

$(B):
	mkdir -p $(B)

ifneq ($(strip $(PIXEL_LIGHTS)),)
$(PIXEL_LIGHTS)/build/libpixel_lights.a:
	$(MAKE) -C $(PIXEL_LIGHTS)
endif

$(B)/claude-light: src/claude_light.c $(PL_DEP) | $(B)
	$(CC) $(CFLAGS) $(PL_CFLAGS) src/claude_light.c $(PL_LINK) -lm -o $@

clean:
	rm -rf $(B)
