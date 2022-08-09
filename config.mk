_VERSION = 0.3.1
VERSION  = `git describe --long --tags --dirty 2>/dev/null || echo $(_VERSION)`

# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man

# Compile flags that can be used
#CFLAGS = -pedantic -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-unused-result -Wdeclaration-after-statement

XWAYLAND =
XLIBS =
# Uncomment to build XWayland support
#XWAYLAND = -DXWAYLAND
#XLIBS = xcb
