# hyprtransition — build / install
#
#   make                 build ./build/hyprtransition (effects are found in ./effects)
#   make install         system-wide into PREFIX (default /usr/local)
#   make install-user    for you only: ~/.local/bin + ~/.config/hypr/hyprtransition/
#   make uninstall / uninstall-user

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/hyprtransition
LUADIR  ?= $(PREFIX)/share/lua/5.5
DESTDIR ?=

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -DDATADIR='"$(DATADIR)"'
PKGS     = wayland-client wayland-egl egl glesv2
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
CFLAGS  += $(shell pkg-config --cflags $(PKGS))

WP       = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XMLS     = protocols/wlr-layer-shell-unstable-v1.xml \
           $(WP)/stable/xdg-shell/xdg-shell.xml \
           $(WP)/stable/viewporter/viewporter.xml
GEN      = $(patsubst %.xml,build/gen/%-protocol.c,$(notdir $(XMLS)))
HDR      = $(patsubst %.xml,build/gen/%-client-protocol.h,$(notdir $(XMLS)))

all: build/hyprtransition

build/hyprtransition: src/hyprtransition.c $(GEN) $(HDR)
	$(CC) $(CFLAGS) -Ibuild/gen -o $@ src/hyprtransition.c $(GEN) $(LDLIBS)

# one rule per XML so make can find each by basename
define gen_rule
build/gen/$(notdir $(basename $(1)))-protocol.c: $(1) | build/gen
	wayland-scanner private-code $$< $$@
build/gen/$(notdir $(basename $(1)))-client-protocol.h: $(1) | build/gen
	wayland-scanner client-header $$< $$@
endef
$(foreach x,$(XMLS),$(eval $(call gen_rule,$(x))))

build/gen:
	mkdir -p $@

install: build/hyprtransition
	install -Dm755 build/hyprtransition $(DESTDIR)$(BINDIR)/hyprtransition
	install -Dm644 lua/hyprtransition.lua $(DESTDIR)$(LUADIR)/hyprtransition.lua
	install -Dm644 -t $(DESTDIR)$(DATADIR)/effects effects/*.glsl

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/hyprtransition $(DESTDIR)$(LUADIR)/hyprtransition.lua
	rm -rf $(DESTDIR)$(DATADIR)

USER_CFG = $(HOME)/.config/hypr/hyprtransition

install-user: build/hyprtransition
	install -Dm755 build/hyprtransition $(HOME)/.local/bin/hyprtransition
	install -Dm644 lua/hyprtransition.lua $(USER_CFG)/init.lua
	install -Dm644 -t $(USER_CFG)/effects effects/*.glsl

uninstall-user:
	rm -f $(HOME)/.local/bin/hyprtransition
	rm -rf $(USER_CFG)

clean:
	rm -rf build

.PHONY: all install uninstall install-user uninstall-user clean
