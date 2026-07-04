# Forward-ported arch/tile Linux 7.1 kernel for Tile-Gx, built with the pkgsCross
# tilegx cross toolchain. Produces vmlinux (+ System.map + config). The .config is
# pinned to the one that boots (carries the P2/P3 fixes: CONFIG_VT=n,
# KALLSYMS_ABSOLUTE, classic SPARSEMEM); the arch/tile Makefiles already carry
# the -fno-tree-loop-distribute-patterns, UTS_MACHINE=tilegx and vdso-syms
# (binutils >= 2.41) fixes from the port.
#
#   nom build --impure .#pkgsCross.tilegx.linux-tilegx
{
  lib,
  stdenv,
  buildPackages,
  bison,
  flex,
  bc,
  perl,
  which,
  cpio,
  elfutils,
  openssl,
  # local port tree + its booting config; overridable
  treeRoot ? /home/amaanq/projects/forks/linux-tilegx/linux-7.1-tile,
}:
let
  tp = stdenv.cc.targetPrefix;
  btp = buildPackages.stdenv.cc.targetPrefix;

  src = lib.cleanSourceWith {
    name = "linux-7.1-tile-src";
    src = treeRoot;
    filter =
      path: type:
      let
        b = baseNameOf path;
      in
      !(
        b == ".git"
        || b == ".jj"
        || b == ".config"
        || b == "vmlinux"
        || b == "vmlinux.o"
        || b == "System.map"
        || lib.hasSuffix ".o" b
        || lib.hasSuffix ".cmd" b
        || lib.hasSuffix ".ko" b
        || lib.hasSuffix ".a" b
        || lib.hasSuffix ".mod" b
        || lib.hasSuffix ".mod.c" b
        || lib.hasInfix "/include/generated/" (toString path)
        || lib.hasInfix "/include/config/" (toString path)
      );
  };
in
stdenv.mkDerivation {
  pname = "linux-tilegx";
  version = "7.1.1";

  inherit src;

  nativeBuildInputs = [
    bison
    flex
    bc
    perl
    which
    cpio
    elfutils
    openssl
  ];

  makeFlags = [
    "ARCH=tile"
    "CROSS_COMPILE=${tp}"
    "LD=${lib.getExe' stdenv.cc.bintools.bintools "${tp}ld"}"
    "AR=${lib.getExe' stdenv.cc "${tp}ar"}"
    "NM=${lib.getExe' stdenv.cc "${tp}nm"}"
    "STRIP=${lib.getExe' stdenv.cc.bintools.bintools "${tp}strip"}"
    "OBJCOPY=${lib.getExe' stdenv.cc "${tp}objcopy"}"
    "OBJDUMP=${lib.getExe' stdenv.cc "${tp}objdump"}"
    "READELF=${lib.getExe' stdenv.cc "${tp}readelf"}"
    "HOSTCC=${lib.getExe' buildPackages.stdenv.cc "${btp}cc"}"
    "HOSTCXX=${lib.getExe' buildPackages.stdenv.cc "${btp}c++"}"
    "HOSTAR=${lib.getExe' buildPackages.stdenv.cc.bintools "${btp}ar"}"
    "HOSTLD=${lib.getExe' buildPackages.stdenv.cc.bintools "${btp}ld"}"
  ];

  enableParallelBuilding = true;

  # cc-wrapper hardening (PIE, stackprotector, fortify) fights the kernel's own
  # flag management; the standalone build used the raw toolchain.
  hardeningDisable = [ "all" ];

  postPatch = ''
    patchShebangs scripts
  '';

  configurePhase = ''
    runHook preConfigure
    cp ${treeRoot + "/.config"} .config
    chmod +w .config
    make ''${makeFlags[@]} olddefconfig
    runHook postConfigure
  '';

  buildFlags = [ "vmlinux" ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp vmlinux $out/vmlinux
    cp System.map $out/System.map
    cp .config $out/config
    runHook postInstall
  '';

  # vmlinux is a target ELF, not a userland binary — no strip/patchelf/rpath fixup.
  dontFixup = true;

  meta = {
    description = "Forward-ported arch/tile Linux 7.1 kernel for Tile-Gx (vmlinux)";
    platforms = [ "tilegx-linux" ];
  };
}
