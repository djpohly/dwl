{ pkgs ? import <nixpkgs> { } }:
let
  enableXWayland = false;
  version = "dc61f471da1a1c9264167635c286b6dcb37b3d6f";
  sha256 = "0k31chpc4facn7n7kmk0s5wp7vj7mpapwk4as6pjhi1rq37g34lf";

  wlroots-git = pkgs.wlroots.overrideAttrs (old: {
    version = version;
    src = pkgs.fetchFromGitHub {
      owner = "swaywm";
      repo = "wlroots";
      rev = version;
      sha256 = sha256;
    };
    buildInputs = old.buildInputs ++ [ pkgs.libuuid ];
  });
in pkgs.mkShell {
  name = "dwl-env";
  nativeBuildInputs = with pkgs; [ cmake pkg-config ];
  buildInputs = with pkgs;
    [
      libGL
      libinput
      libxkbcommon
      pixman
      wayland
      wayland-protocols
      wlroots-git
      xorg.libxcb
    ] ++ pkgs.lib.optional enableXWayland [ x11 ];
}
