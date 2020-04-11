# dwl

This is the "minimum viable product" Wayland compositor based on wlroots. It
aims to implement a Wayland compositor in the fewest lines of code possible,
while still supporting a reasonable set of features. Reading this code is the
best starting point for anyone looking to build their own Wayland compositor
based on wlroots.


## Building dwl

dwl is disconnected from the main wlroots build system, in order to make it
easier to understand the build requirements for your own Wayland compositors.
Simply install the dependencies:

- wlroots
- wayland-protocols

And run `make`.


## Running dwl

You can run dwl with `./dwl`. In an existing Wayland or X11 session,
dwl will open a Wayland or X11 window respectively to act as a virtual
display. You can then open Wayland windows by setting `WAYLAND_DISPLAY` to the
value shown in the logs. You can also run `./dwl` from a TTY.

In either case, you will likely want to specify `-s [cmd]` to run a command at
startup, such as a terminal emulator. This will be necessary to start any new
programs from within the compositor, as dwl does not support any custom
keybindings. dwl supports the following keybindings:

- `Alt+Escape`: Terminate the compositor
- `Alt+F1`: Cycle between windows


## Limitations

Notable omissions from dwl:

- HiDPI support
- Any kind of configuration, e.g. output layout
- Any protocol other than xdg-shell (e.g. layer-shell, for
  panels/taskbars/etc; or Xwayland, for proxied X11 windows)
- Optional protocols, e.g. screen capture, primary selection, virtual
  keyboard, etc. Most of these are plug-and-play with wlroots, but they're
  omitted for brevity.
- Damage tracking, which tracks which parts of the screen are changing and
  minimizes redraws accordingly.
