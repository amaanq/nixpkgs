{ lib, pkgs }:

pkgs.replaceVarsWith {
  src = ./zipl-builder.sh;
  isExecutable = true;
  replacements = {
    path = lib.makeBinPath [
      pkgs.coreutils
      pkgs.gnused
      pkgs.gnugrep
      pkgs.s390-tools
    ];
    inherit (pkgs) bash;
  };
}
