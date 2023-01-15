wayland_scanner = find_program('wayland-scanner')
wayland_protos_dep = dependency('wayland-protocols')
wl_protocol_dir = wayland_protos_dep.get_pkgconfig_variable('pkgdatadir')
wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'])

wayland_xmls = [
	wl_protocol_dir + '/stable/xdg-shell/xdg-shell.xml',
	'wlr-layer-shell-unstable-v1.xml',
	'net-tapesoftware-dwl-wm-unstable-v1.xml',
]
wayland_sources = [
	wayland_scanner_code.process(wayland_xmls),
	wayland_scanner_client.process(wayland_xmls),
]
