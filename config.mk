# paths
PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man

# Default compile flags (overridable by environment)
CFLAGS ?= -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-unused-result -Wdeclaration-after-statement

# Uncomment to build XWayland support
#CFLAGS += -DXWAYLAND
