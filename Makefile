.POSIX:
.SUFFIXES:
include config.mk
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L -DVERSION=\"$(VERSION)\" $(XWAYLAND)
DWLDEVCFLAGS = -pedantic -Wall -Wextra -Wdeclaration-after-statement -Wno-unused-parameter -Wno-sign-compare -Wshadow -Wunused-macros\
	-Werror=strict-prototypes -Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types
PKGS      = wlroots wayland-server xkbcommon libinput $(XLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(LIBS)
dwl: dwl.o util.o
	$(CC) dwl.o util.o $(LDLIBS) $(LDFLAGS) $(DWLCFLAGS) -o $@
dwl.o: dwl.c config.mk config.h client.h xdg-shell-protocol.h wlr-layer-shell-unstable-v1-protocol.h
util.o: util.c util.h
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
clean:
	rm -f dwl *.o *-protocol.h
install: dwl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f dwl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/dwl
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/dwl
.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -c $<