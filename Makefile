include config.mk

CFLAGS += -I. -DWLR_USE_UNSTABLE -std=c99 -Werror=declaration-after-statement

WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)

PKGS = wlroots wayland-server xcb xkbcommon libinput
CFLAGS += $(foreach p,$(PKGS),$(shell pkg-config --cflags $(p)))
LDLIBS += $(foreach p,$(PKGS),$(shell pkg-config --libs $(p)))


# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

xdg-shell-protocol.o: xdg-shell-protocol.h

config.h: | config.def.h
	cp config.def.h $@

dwl.o: config.h xdg-shell-protocol.h

dwl: xdg-shell-protocol.o

clean:
	rm -f dwl *.o xdg-shell-protocol.h xdg-shell-protocol.c

install: dwl
	install -D dwl $(PREFIX)/bin/dwl

.DEFAULT_GOAL=dwl
.PHONY: clean
