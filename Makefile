.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -DVERSION=\"$(VERSION)\"

# Wayland utils
WAYLAND_PROTOCOLS = `pkg-config --variable=pkgdatadir wayland-protocols`
WAYLAND_SCANNER   = `pkg-config --variable=wayland_scanner wayland-scanner`

# CFLAGS / LDFLAGS
PKGS      = wlroots wayland-server xkbcommon libinput $(XLIBS)
DWLCFLAGS = `pkg-config --cflags $(PKGS)` $(DWLCPPFLAGS) $(CFLAGS) $(XWAYLAND)
LDLIBS    = `pkg-config --libs $(PKGS)`

# build rules

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
all: dwl
dwl: dwl.o xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o idle-protocol.o util.o
	$(CC) $(LDLIBS) -o $@ dwl.o xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o idle-protocol.o util.o
dwl.o: dwl.c config.mk config.h client.h xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h idle-protocol.h
xdg-shell-protocol.o: xdg-shell-protocol.h xdg-shell-protocol.c
wlr-layer-shell-unstable-v1-protocol.o: wlr-layer-shell-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.c
idle-protocol.o: idle-protocol.h idle-protocol.c
util.o: util.c util.h

# wayland scanner rules to generate .h / .c files
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/wlr-layer-shell-unstable-v1.xml $@
idle-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/idle.xml $@
idle-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/idle.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f dwl *.o *-protocol.h *-protocol.c

# distribution archive
dist: clean
	mkdir -p dwl-$(VERSION)
	cp -R LICENSE* Makefile README.md generate-version.sh client.h\
		config.def.h config.mk protocols dwl.1 dwl.c util.c util.h\
		dwl-$(VERSION)
	echo "echo $(VERSION)" > dwl-$(VERSION)/generate-version.sh
	tar -caf dwl-$(VERSION).tar.gz dwl-$(VERSION)
	rm -rf dwl-$(VERSION)

# install rules

install: dwl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp dwl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwl
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp dwl.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/dwl.1
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwl $(DESTDIR)$(MANDIR)/man1/dwl.1

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -c $<
