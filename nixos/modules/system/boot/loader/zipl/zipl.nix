{
  config,
  lib,
  pkgs,
  ...
}:

with lib;

let
  blCfg = config.boot.loader;
  cfg = blCfg.zipl;

  timeoutStr = if blCfg.timeout == null then "0" else toString blCfg.timeout;

  builder = import ./zipl-builder.nix { inherit lib pkgs; };
  populateBuilder = import ./zipl-builder.nix {
    inherit lib;
    pkgs = pkgs.buildPackages;
  };
in
{
  options = {
    boot.loader.zipl = {
      enable = mkOption {
        default = false;
        type = types.bool;
        description = ''
          Whether to use the zipl bootloader for IBM s390x systems.

          zipl (z/Architecture Initial Program Loader) is the standard
          bootloader for Linux on IBM Z and LinuxONE hardware, as well
          as s390x QEMU virtual machines.
        '';
      };

      configurationLimit = mkOption {
        default = 20;
        example = 10;
        type = types.int;
        description = ''
          Maximum number of configurations in the boot menu.
          zipl supports up to 62 menu entries on SCSI devices.
        '';
      };

      bootPath = mkOption {
        default = "/boot";
        type = types.str;
        description = ''
          Path to the boot directory where the zipl configuration and
          kernel/initrd files will be stored. This directory must reside
          on the device that zipl will install the boot record to.
        '';
      };

      populateCmd = mkOption {
        type = types.str;
        readOnly = true;
        description = ''
          Contains the builder command used to populate a disk image,
          honoring all options except the `-c <path-to-default-configuration>`
          argument.
        '';
      };
    };
  };

  config =
    let
      builderArgs = "-g ${toString cfg.configurationLimit} -t ${timeoutStr}";
      installBootLoader = pkgs.writeScript "install-zipl.sh" ''
        #!${pkgs.runtimeShell}
        set -e
        ${builder} ${builderArgs} -d '${cfg.bootPath}' -c "$@"
      '';
    in
    mkIf cfg.enable {
      system.build.installBootLoader = installBootLoader;
      system.boot.loader.id = "zipl";

      boot.loader.zipl.populateCmd = "${populateBuilder} ${builderArgs}";

      environment.systemPackages = [ pkgs.s390-tools ];
    };
}
