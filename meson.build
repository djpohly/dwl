project(
	'dwl',
	'c',
)

add_project_arguments(
	[
		'-DWLR_USE_UNSTABLE',
    '-DXWAYLAND', # XWayland support
    # C compiler options
    '-g',
    '-Wall',
    '-Wextra',
    '-Werror',
		'-Wno-unused-parameter',
    '-Wno-sign-compare',
    '-Wno-error=unused-function',
		'-Wno-unused-result',
		'-Wno-missing-braces',
		'-Wundef',
		'-Wvla',
    '-std=c99',
    '-Werror=declaration-after-statement',
	],
	language: 'c',
)

### Generate xdg-shell-protocol files
# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
# TODO(vkasljevic):
#
# WAYLAND_PROTOCOLS=$(shell pkg-config --variable=pkgdatadir wayland-protocols)
# WAYLAND_SCANNER=$(shell pkg-config --variable=wayland_scanner wayland-scanner)
#
# xdg-shell-protocol.h:
# $(WAYLAND_SCANNER) server-header \
# 	$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
# xdg-shell-protocol.c:
# $(WAYLAND_SCANNER) private-code \
# 	$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
#
xdg_shell_protocol_c = custom_target(
    'xdg-shell-protocol.c',
    output : 'xdg-shell-protocol.c',
    command : ['wayland-scanner', 'private-code', '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml', '@OUTPUT@'],
)

xdg_shell_protocol_h = custom_target(
    'xdg-shell-protocol.h',
    output : 'xdg-shell-protocol.h',
    command : ['wayland-scanner', 'server-header', '/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml', '@OUTPUT@'],
)

xdg_shell_protocol = static_library('xdg-shell-protocol', [xdg_shell_protocol_h, xdg_shell_protocol_c])

### List all dependencies. They must be present on system before compiling.
# wayland-scanner
# wayland-protocols
dependencies = [
  dependency('pixman-1'),
  dependency('wlroots'),
  dependency('wayland-server'),
  dependency('xkbcommon'),
  dependency('xcb')
]

executable(
  'dwl',
  'dwl.c',
  dependencies : dependencies,
  link_with : xdg_shell_protocol
)

