# To build, use:
# nix-build nixos -I nixos-config=nixos/modules/installer/sd-card/sd-image-s390x-qemu.nix -A config.system.build.sdImage
{
  config,
  lib,
  pkgs,
  ...
}:

{
  imports = [
    ../../profiles/base.nix
    ../../profiles/installation-device.nix
    ./sd-image.nix
  ];

  boot.loader = {
    grub.enable = false;
    generic-extlinux-compatible.enable = false;
    zipl.enable = true;
  };

  boot.consoleLogLevel = lib.mkDefault 7;
  boot.kernelParams = [ "console=ttysclp0" ];

  sdImage = {
    populateFirmwareCommands = "";
    populateRootCommands = ''
      mkdir -p ./files/boot
      ${config.boot.loader.zipl.populateCmd} -c ${config.system.build.toplevel} -d ./files/boot
    '';
  };
}
