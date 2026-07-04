# nix-native build of the forward-ported 7.1 arch/tile kernel. Produces the same
# vmlinux the raw `make` did, but reproducibly under nix, using the pkgsCross
# tilegx cross toolchain (the same GCC-15 backend glibc was built with).
#
#   nix build --impure -f tile-kernel.nix -o ~/tile-vmlinux -L
#
# The .config is pinned to the one that produced the booting #20 kernel (carries
# the P2/P3 fixes: CONFIG_VT=n, KALLSYMS_ABSOLUTE, classic SPARSEMEM). The
# arch/tile Makefiles already carry the -fno-tree-loop-distribute-patterns and
# UTS_MACHINE=tilegx fixes from the port, so nothing extra is needed here.
{
  nixpkgs ? ./.,
}:
let
  cross = (import nixpkgs { config.allowUnsupportedSystem = true; }).pkgsCross.tilegx;
  inherit (cross) stdenv lib;
  build = cross.buildPackages;

  tp = stdenv.cc.targetPrefix;
  btp = build.stdenv.cc.targetPrefix;

  treeRoot = /home/amaanq/projects/forks/linux-tilegx/linux-7.1-tile;

  # Pin the booting config out-of-band so src filtering can't drop it and a
  # stale in-tree .config can't shadow it. Must be a path (not a string) so nix
  # copies it into the build sandbox.
  kconfig = treeRoot + "/.config";

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

  toolFlags = [
    "ARCH=tile"
    "CROSS_COMPILE=${tp}"
    "LD=${lib.getExe' stdenv.cc.bintools.bintools "${tp}ld"}"
    "AR=${lib.getExe' stdenv.cc "${tp}ar"}"
    "NM=${lib.getExe' stdenv.cc "${tp}nm"}"
    "STRIP=${lib.getExe' stdenv.cc.bintools.bintools "${tp}strip"}"
    "OBJCOPY=${lib.getExe' stdenv.cc "${tp}objcopy"}"
    "OBJDUMP=${lib.getExe' stdenv.cc "${tp}objdump"}"
    "READELF=${lib.getExe' stdenv.cc "${tp}readelf"}"
    "HOSTCC=${lib.getExe' build.stdenv.cc "${btp}cc"}"
    "HOSTCXX=${lib.getExe' build.stdenv.cc "${btp}c++"}"
    "HOSTAR=${lib.getExe' build.stdenv.cc.bintools "${btp}ar"}"
    "HOSTLD=${lib.getExe' build.stdenv.cc.bintools "${btp}ld"}"
  ];
in
stdenv.mkDerivation {
  pname = "linux-tilegx";
  version = "7.1.1";

  inherit src;

  nativeBuildInputs = with build; [
    bison
    flex
    bc
    perl
    which
    cpio
    elfutils
    openssl
  ];

  makeFlags = toolFlags;
  enableParallelBuilding = true;

  # cc-wrapper hardening (PIE, stackprotector, fortify) fights the kernel's own
  # flag management; the standalone build used the raw toolchain.
  hardeningDisable = [ "all" ];

  postPatch = ''
    patchShebangs scripts
  '';

  configurePhase = ''
    runHook preConfigure
    cp ${kconfig} .config
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
    platforms = [ "tilegx-unknown-linux-gnu" ];
  };
}
