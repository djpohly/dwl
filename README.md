# dwl - dwm for Wayland

dwl is a compact, hackable compositor for Wayland based on
[wlroots](https://github.com/swaywm/wlroots).  It is intended to fill the same
space in the Wayland world that dwm does in X11, primarily in terms of
philosophy, and secondarily in terms of functionality.  Like dwm, dwl is:

- Easy to understand, hack on, and extend with patches
- One C source file (or a very small number) configurable via `config.h`
- Limited to a maximum number of SLOC (to be determined)
- Tied to as few external dependencies as possible


dwl is not meant to provide every feature under the sun.  Instead, like dwm, it
sticks to features which are necessary, simple, and straightforward to
implement given the base on which it is built.  Since wlroots provides a number
of features that are more complicated to accomplish with Xlib and select
extensions, dwl can be in some ways more featureful than dwm *while remaining
just as simple.*  Intended default features are:

- Any features provided by dwm/Xlib: simple window borders, tags, keybindings,
  client rules, mouse move/resize (see below for why the built-in status bar is
  a possible exception)
- Configurable multi-monitor layout support, including position and rotation
- Configurable HiDPI/multi-DPI support
- Wayland protocols needed for daily life in the tiling world: at a minimum,
  xdg-shell and layer-shell (for bars/menus).  Protocols trivially provided by
  wlroots may also be added.
- Basic yes/no damage tracking to avoid needless redraws (if it can be done
  simply and has an impact on power consumption)


Other features under consideration are:

- Additional Wayland compositor protocols which are trivially provided by
  wlroots or can be conditionally included via `config.h` settings: xwayland,
  xdg-portal, etc.
- External bar support instead of a built-in status bar, to avoid taking a
  dependency on FreeType or Pango
- More in-depth damage region tracking


## Building dwl

dwl has only two dependencies: wlroots and wayland-protocols.  Simply install
these and run `make`.


## Configuration

All configuration is done by editing `config.h` and recompiling, in the same
manner as dwm.  There is no way to separately restart the window manager in
Wayland without restarting the entire display server, so any changes will take
effect the next time dwl is executed.


## Running dwl

dwl can be run as-is, with no arguments. In an existing Wayland or X11 session,
this will open a window to act as a virtual display.  When run from a TTY, the
Wayland server will take over the entire virtual terminal.  Clients started by
dwl will have `WAYLAND_DISPLAY` set in their environment, and other clients can be
started from outside the session by setting this variable accordingly.

You can also specify a startup program using the `-s` option.  The argument to
this option will be run at startup as a shell command (using `sh -c`) and can
serve a similar function to `.xinitrc`: starting a service manager or other
startup applications.  Unlike `.xinitrc`, the display server will not shut down
when this process terminates.  Instead, as dwl is shutting down, it will send
this process a SIGTERM and wait for it to terminate (if it hasn't already).
This make it ideal not only for initialization but also for execing into a
user-level service manager like s6 or `systemd --user`.


## Known limitations and issues

dwl is a work in progress, and it has not yet reached its feature goals in a
number of ways:

- Features not yet implemented:
    - xdg-shell popups
    - Urgent/attention/focus-request
    - Borders and selected/normal/urgent colors
    - layer-shell
    - Statusbar support (built-in or external)
    - Damage tracking
    - Fullscreen/fixed windows (or whatever the Wayland analogues are)
    - XWayland
- HiDPI works, but multi-DPI is not as nice as sway, depending on the scale
  factors involved.  Perhaps scaling filters are needed?
- Monitor rotation/transform is not set up yet
- Mouse resize is not precise


## Acknowledgements

dwl began by extending the TinyWL example provided (CC0) by the sway/wlroots
developers.  This was made possible in many cases by looking at how sway
accomplished something, then trying to do the same in as suckless a way as
possible.  Speaking of which, many thanks to suckless.org and the dwm
developers and community for the inspiration.
