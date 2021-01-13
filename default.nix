{ pkgs ? import <nixpkgs> { } }:
let
  enableXWayland = false;
  version = "4f80fab337f10b4ad2043b834606540895b8df36";
  sha256 = "0zsfglyfmzsxf6vkdv999z49v66pwcyffb0pn824ciafwna2408r";

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
