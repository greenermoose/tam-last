# tam-last: the Tamlinux terminal. Plain Make; no other build system.
#
#   make            build build/tam-last
#   make test       unit tests, then the headless check (needs tam-pane)
#   make sanitize   build in build/sanitize with ASan and UBSan, run the tests
#   make install    install to PREFIX (default $HOME/.local)
#   make uninstall  remove what install put there
#   make clean      remove build/
#
# The only dependencies are the C library and libtam, linked statically
# from LIBTAM_PREFIX (see arch/dependencies.md). No pkg-config, no
# wayland-scanner: the Wayland code is our own.

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
TERMINFODIR ?= $(DATADIR)/terminfo
TIC ?= tic
LIBTAM_PREFIX ?= $(HOME)/.local
# A tam-pane build for the headless check; skipped when it is missing.
TAM_PANE ?= ../tam-pane/build/tam-pane

# Where objects and binaries go; make sanitize uses build/sanitize.
B ?= build

VERSION := $(shell tr -d '[:space:]' < VERSION)

CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wvla \
	-D_XOPEN_SOURCE=700 -DTL_VERSION='"$(VERSION)"' \
	-Iinclude -I$(LIBTAM_PREFIX)/include \
	-ffile-prefix-map=$(CURDIR)=. -MMD -MP
LIBTAM := $(LIBTAM_PREFIX)/lib/libtam.a
LDLIBS = $(LIBTAM) -lm

# Compiled-in keyboard layouts (us first so 'include us' resolves) and compose.
LAYOUT_SRCS = layouts/us.layout layouts/us-intl.layout layouts/dvorak.layout \
	layouts/colemak.layout layouts/uk.layout layouts/de.layout \
	layouts/fr.layout
COMPOSE_SRC = layouts/compose

# Everything but main.c, so the unit tests can link it.
CORE_SRCS = src/wl/conn.c src/wl/proto.c src/window.c src/shm.c \
	src/font/ttf.c src/font/raster.c src/font/box.c src/font/font.c \
	src/config.c src/vt.c src/osc.c src/term.c src/select.c src/search.c \
	src/url.c src/action.c src/ipc.c src/render.c \
	src/keys/keymap.c src/keys/compose.c src/keys/encode.c \
	src/keys/trace.c src/pty.c
CORE_OBJS = $(patsubst src/%.c,$(B)/%.o,$(CORE_SRCS)) $(B)/keys/layouts_gen.o
TESTS = $(B)/tests/test_wire $(B)/tests/test_vt $(B)/tests/test_ttf \
	$(B)/tests/test_keys $(B)/tests/test_render $(B)/tests/test_config \
	$(B)/tests/test_modes $(B)/tests/test_ipc

all: $(B)/tam-last $(B)/terminfo/t/tam-last

$(B)/terminfo/t/tam-last: terminfo/tam-last.ti
	@mkdir -p $(B)/terminfo
	$(TIC) -x -o $(B)/terminfo terminfo/tam-last.ti

$(B)/gen-layouts: tools/gen-layouts.c include/tam-last/evdev.h include/tam-last/keys.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/gen-layouts.c

$(B)/layouts_gen.c: $(B)/gen-layouts $(LAYOUT_SRCS) $(COMPOSE_SRC)
	$(B)/gen-layouts -o $@ -c $(COMPOSE_SRC) $(LAYOUT_SRCS)

$(B)/keys/layouts_gen.o: $(B)/layouts_gen.c include/tam-last/keys.h include/tam-last/evdev.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/tam-last: $(B)/main.o $(CORE_OBJS) $(LIBTAM)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(B)/main.o $(CORE_OBJS) $(LDLIBS)

$(B)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# main.c and term.c report the version, so rebuild them when VERSION changes.
$(B)/main.o $(B)/term.o: VERSION

$(B)/tests/test_%: tests/test_%.c $(CORE_OBJS) $(LIBTAM) tests/check.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Itests $(LDFLAGS) -o $@ $< $(CORE_OBJS) $(LDLIBS)

test: $(TESTS) $(B)/tam-last $(B)/terminfo/t/tam-last
	@for t in $(TESTS); do $$t $(B)/tests || exit 1; done
	@test "$$($(B)/tam-last --version)" = "tam-last $(VERSION)" || \
		{ echo "version: FAILED (binary does not say $(VERSION))"; exit 1; }
	@echo "version: tam-last $(VERSION): ok"
	@TERMINFO=$(B)/terminfo infocmp -x tam-last >/dev/null && \
		TERMINFO=$(B)/terminfo infocmp -x tam-last-direct >/dev/null || \
		{ echo "terminfo: FAILED"; exit 1; }
	@echo "terminfo: tam-last, tam-last-direct: ok"
	@sh tests/headless.sh $(B)/tam-last $(TAM_PANE)

sanitize:
	$(MAKE) B=build/sanitize \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' \
		test

install: $(B)/tam-last
	install -Dm755 $(B)/tam-last $(DESTDIR)$(BINDIR)/tam-last
	ln -sf tam-last $(DESTDIR)$(BINDIR)/tam-last-client
	mkdir -p $(DESTDIR)$(TERMINFODIR)
	$(TIC) -x -o $(DESTDIR)$(TERMINFODIR) terminfo/tam-last.ti

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/tam-last \
		$(DESTDIR)$(BINDIR)/tam-last-client \
		$(DESTDIR)$(TERMINFODIR)/t/tam-last \
		$(DESTDIR)$(TERMINFODIR)/t/tam-last-direct

clean:
	rm -rf build

.PHONY: all test sanitize install uninstall clean

-include $(shell find $(B) -name '*.d' 2>/dev/null)
