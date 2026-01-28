{
  lib,
  fetchFromGitHub,
  rustPlatform,
  pkg-config,
  dbus,
}:

rustPlatform.buildRustPackage rec {
  version = "0.2.2-pre-unstable-2025-03-27";
  pname = "kdotool";

  src = fetchFromGitHub {
    owner = "jinliu";
    repo = "kdotool";
    rev = "1ad61acb56c0707df53a4d9ce10153a87f08523b";
    hash = "sha256-zGuq8YjxLRSd26UfRgBfKveesZy6ruVLQlTbGN/afoE=";
  };

  cargoHash = "sha256-CZr/aPAPFjeJdlF8wvf1c16bBGhzGhVW3WnZJ8TC68A=";

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ dbus ];

  meta = {
    description = "xdotool clone for KDE Wayland";
    homepage = "https://github.com/jinliu/kdotool";
    license = lib.licenses.asl20;
    maintainers = with lib.maintainers; [ kotatsuyaki ];
  };
}
