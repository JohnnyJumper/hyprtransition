# hyprtransition — build / install
#
#   make                 build ./build/hyprtransition (effects are found in ./effects)
#   make install         system-wide into PREFIX (default /usr/local)
#   make install-user    for you only: ~/.local/bin + ~/.local/share/hyprtransition/ (nothing in ~/.config)
#   make uninstall / uninstall-user

VERSION  = 0.0.1
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/hyprtransition
LUADIR  ?= $(PREFIX)/share/lua/5.5
DESTDIR ?=

CC      ?= cc
CFLAGS  ?= -O2
override CFLAGS += -Wall -Wextra -DDATADIR='"$(DATADIR)"' -DVERSION='"$(VERSION)"'
# whichever Lua pkg-config name this distro uses (Hyprland embeds 5.5; 5.4 works too)
LUA_PKG ?= $(shell for p in lua5.5 lua-5.5 lua lua5.4 lua-5.4; do pkg-config --exists $$p && echo $$p && break; done)
PKGS     = wayland-client wayland-egl egl glesv2 $(LUA_PKG)
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
CFLAGS  += $(shell pkg-config --cflags $(PKGS))

WP       = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XMLS     = protocols/wlr-layer-shell-unstable-v1.xml \
           $(WP)/stable/xdg-shell/xdg-shell.xml \
           $(WP)/stable/viewporter/viewporter.xml
GEN      = $(patsubst %.xml,build/gen/%-protocol.c,$(notdir $(XMLS)))
HDR      = $(patsubst %.xml,build/gen/%-client-protocol.h,$(notdir $(XMLS)))

all: build/hyprtransition

build/hyprtransition: src/hyprtransition.c $(GEN) $(HDR) build/gen/shaders.h
	$(CC) $(CFLAGS) -Ibuild/gen -o $@ src/hyprtransition.c $(GEN) $(LDLIBS)

SHADERS = src/vertex.glsl src/prelude.glsl src/postlude.glsl
build/gen/shaders.h: $(SHADERS) scripts/embed-glsl.sh | build/gen
	{ ./scripts/embed-glsl.sh VERTEX_SHADER src/vertex.glsl; \
	  ./scripts/embed-glsl.sh EFFECT_PRELUDE src/prelude.glsl; \
	  ./scripts/embed-glsl.sh EFFECT_POSTLUDE src/postlude.glsl; } > $@

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
	install -Dm644 config.example.lua $(DESTDIR)$(DATADIR)/config.example.lua

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/hyprtransition $(DESTDIR)$(LUADIR)/hyprtransition.lua
	rm -rf $(DESTDIR)$(DATADIR)

XDG_DATA_HOME ?= $(HOME)/.local/share
USER_DATA      = $(XDG_DATA_HOME)/hyprtransition

install-user: build/hyprtransition
	install -Dm755 build/hyprtransition $(HOME)/.local/bin/hyprtransition
	install -Dm644 lua/hyprtransition.lua $(USER_DATA)/hyprtransition.lua
	install -Dm644 -t $(USER_DATA)/effects effects/*.glsl
	install -Dm644 config.example.lua $(USER_DATA)/config.example.lua
	@echo
	@echo "Installed. Add to the end of your hyprland.lua:"
	@echo '  dofile(os.getenv("HOME") .. "/.local/share/hyprtransition/hyprtransition.lua").setup()'
	@echo "Settings (optional):"
	@echo '  mkdir -p ~/.config/hyprtransition && cp $(USER_DATA)/config.example.lua ~/.config/hyprtransition/config.lua'

uninstall-user:
	rm -f $(HOME)/.local/bin/hyprtransition
	rm -rf $(USER_DATA)

check: build/hyprtransition
	./scripts/check-effects.sh
	for f in lua/*.lua config.example.lua; do luac -p $$f && echo "ok    $$f"; done

clean:
	rm -rf build

.PHONY: all check install uninstall install-user uninstall-user clean
