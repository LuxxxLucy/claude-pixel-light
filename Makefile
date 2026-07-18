CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PREFIX ?= /usr/local
B       = build

.PHONY: all clean install uninstall

all: $(B)/claude-light

$(B):
	mkdir -p $(B)

$(B)/claude-light: src/claude_light.c | $(B)
	$(CC) $(CFLAGS) $(LDFLAGS) src/claude_light.c -o $@

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(B)/claude-light $(DESTDIR)$(PREFIX)/bin/claude-light

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/claude-light

clean:
	rm -rf $(B)
