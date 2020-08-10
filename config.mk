# Default compile flags (overridable by environment)
CFLAGS ?= -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-error=unused-function

# Uncomment to build XWayland support
#CFLAGS += -DXWAYLAND
