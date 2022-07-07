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
LDLIBS    = `pkg-config --libs $(PKGS)` $(LIBS)

# build rules

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
all: dwl
dwl: dwl.o util.o
	$(CC) dwl.o util.o $(LDLIBS) $(LDFLAGS) $(DWLCFLAGS) -o $@
dwl.o: dwl.c config.mk config.h client.h xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h idle-protocol.h
util.o: util.c util.h

# wayland scanner rules to generate .h / .c files
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
idle-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/idle.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f dwl *.o *-protocol.h

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
	cp -f dwl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwl
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f dwl.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/dwl.1
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwl $(DESTDIR)$(MANDIR)/man1/dwl.1

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -c $<
