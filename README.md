# dwl - dwm for Wayland

Join us on our [Discord server](https://discord.gg/jJxZnrGPWN)!

dwl is a compact, hackable compositor for Wayland based on [wlroots](https://github.com/swaywm/wlroots). It is intended to fill the same space in the Wayland world that dwm does in X11, primarily in terms of philosophy, and secondarily in terms of functionality. Like dwm, dwl is:

- Easy to understand, hack on, and extend with patches
- One C source file (or a very small number) configurable via `config.h`
- Limited to 2000 SLOC to promote hackability
- Tied to as few external dependencies as possible

dwl is not meant to provide every feature under the sun. Instead, like dwm, it sticks to features which are necessary, simple, and straightforward to implement given the base on which it is built. Implemented default features are:

- Any features provided by dwm/Xlib: simple window borders, tags, keybindings, client rules, mouse move/resize. Providing a built-in status bar is an exception to this goal, to avoid dependencies on font rendering and/or drawing libraries when an external bar could work well.
- Configurable multi-monitor layout support, including position and rotation
- Configurable HiDPI/multi-DPI support
- Various Wayland protocols
- XWayland support as provided by wlroots
- Zero flickering - Wayland users naturally expect that "every frame is perfect"

Features under consideration (possibly as patches) are:

- Protocols made trivial by wlroots
- Provide information to external status bars via stdout or another file descriptor
- Implement the input-inhibitor protocol to support screen lockers
- Implement the idle-inhibit protocol which lets applications such as mpv disable idle monitoring
- Layer shell popups (used by Waybar)
- Basic yes/no damage tracking to avoid needless redraws
- More in-depth damage region tracking ([which may improve power usage](https://mozillagfx.wordpress.com/2019/10/22/dramatically-reduced-power-usage-in-firefox-70-on-macos-with-core-animation/))
- Implement the text-input and input-method protocols to support IME once ibus implements input-method v2 (see https://github.com/ibus/ibus/pull/2256 and https://github.com/djpohly/dwl/pull/12)
- Implement urgent/attention/focus-request once it's part of the xdg-shell protocol (https://gitlab.freedesktop.org/wayland/wayland-protocols/-/merge_requests/9)

Feature *non-goals* include:

- Client-side decoration (any more than is necessary to tell the clients not to)
- Client-initiated window management, such as move, resize, and close, which can be done through the compositor

## Building dwl

dwl has only two dependencies: wlroots 0.13 and wayland-protocols. Simply install these and run `make`.  If you wish to build against a Git version of wlroots, check out the [wlroots-next branch](https://github.com/djpohly/dwl/tree/wlroots-next).

To enable XWayland, you should also install xorg-xwayland and uncomment its flag in `config.mk`.

## Configuration

All configuration is done by editing `config.h` and recompiling, in the same manner as dwm. There is no way to separately restart the window manager in Wayland without restarting the entire display server, so any changes will take effect the next time dwl is executed.

As in the dwm community, we encourage users to share patches they have created.  Check out the [patches page on our wiki](https://github.com/djpohly/dwl/wiki/Patches)!

## Running dwl

dwl can be run as-is, with no arguments. In an existing Wayland or X11 session, this will open a window to act as a virtual display. When run from a TTY, the Wayland server will take over the entire virtual terminal. Clients started by dwl will have `WAYLAND_DISPLAY` set in their environment, and other clients can be started from outside the session by setting this variable accordingly.

You can also specify a startup program using the `-s` option. The argument to this option will be run at startup as a shell command (using `sh -c`) and can serve a similar function to `.xinitrc`: starting a service manager or other startup applications. Unlike `.xinitrc`, the display server will not shut down when this process terminates. Instead, as dwl is shutting down, it will send this process a SIGTERM and wait for it to terminate (if it hasn't already). This makes it ideal not only for initialization but also for execing into a user-level service manager like s6 or `systemd --user`.

Note: Wayland requires a valid `XDG_RUNTIME_DIR`, which is usually set up by a session manager such as `elogind` or `systemd-logind`.  If your system doesn't do this automatically, you will need to configure it prior to launching `dwl`, e.g.:

    export XDG_RUNTIME_DIR=/tmp/xdg-runtime-$(id -u)
    mkdir -p $XDG_RUNTIME_DIR

## Replacements for X applications

You can find a [list of Wayland applications on the sway wiki](https://github.com/swaywm/sway/wiki/i3-Migration-Guide).

## IRC channel

dwl's IRC channel is #dwl on irc.freenode.net.

## Acknowledgements

dwl began by extending the TinyWL example provided (CC0) by the sway/wlroots developers. This was made possible in many cases by looking at how sway accomplished something, then trying to do the same in as suckless a way as possible.

Many thanks to suckless.org and the dwm developers and community for the inspiration, and to the various contributors to the project, including:

- Alexander Courtis for the XWayland implementation
- Guido Cella for the layer-shell protocol implementation, patch maintenance, and for helping to keep the project running
- Stivvo for output management and fullscreen support, and patch maintenance
