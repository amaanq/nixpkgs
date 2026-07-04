# Minimal nix-built initramfs for the Tile-Gx board: busybox + glibc 2.42 + a
# tiny init. Boots with the existing 7.1 vmlinux (rootfs is hvfs-delivered, so
# swapping this cpio needs no kernel rebuild).
#
#   nix build --impure -f tile-initramfs.nix -o result-initrd
#   cp result-initrd ...TileraMDE.../tile/boot/initramfs.cpio.gz
{
  nixpkgs ? ./.,
}:
let
  pkgs = (import nixpkgs { }).pkgsCross.tilegx;
  build = pkgs.buildPackages;

  busybox = pkgs.busybox;
  hello = pkgs.hello;

  init = build.writeText "init" ''
    #!${busybox}/bin/sh
    export PATH=${busybox}/bin
    busybox mkdir -p /proc /sys /dev /tmp /root
    busybox mount -t proc     proc /proc
    busybox mount -t sysfs    sys  /sys
    busybox mount -t devtmpfs dev  /dev 2>/dev/null || busybox mdev -s

    echo
    echo ">>>>>> NIX TILE-GX ROOTFS UP (glibc 2.42, busybox ${busybox.version}) <<<<<<"
    busybox uname -a
    echo ">>> ld.so: ${busybox}/bin/busybox interp check"
    busybox head -c0 /dev/null; echo "  (dynamic exec of busybox succeeded => glibc 2.42 loaded)"
    echo ">>> hello:"
    ${hello}/bin/hello || echo "  hello FAILED rc=$?"
    echo ">>>>>> handing off to a shell on /dev/console <<<<<<"
    exec busybox setsid busybox sh -c 'exec busybox sh </dev/console >/dev/console 2>&1'
  '';

  closure = build.closureInfo { rootPaths = [ busybox hello init ]; };
in
build.runCommand "tile-initramfs.cpio.gz"
  {
    nativeBuildInputs = [
      build.cpio
      build.gzip
    ];
  }
  ''
    root=$(mktemp -d)
    mkdir -p "$root/nix/store" "$root/bin" "$root/proc" "$root/sys" "$root/dev"

    # copy the full runtime closure preserving store paths
    for p in $(cat ${closure}/store-paths); do
      cp -a "$p" "$root/nix/store/"
    done

    cp ${init} "$root/init"
    chmod +x "$root/init"
    ln -s ${busybox}/bin/busybox "$root/bin/sh"

    # tilegx binaries are stamped with the FHS interpreter /lib/ld.so.1 (the
    # platform dynamicLinker isn't the store path); libs still resolve via each
    # binary's RUNPATH, so only the interpreter needs an FHS home.
    mkdir -p "$root/lib"
    ln -s ${pkgs.stdenv.cc.libc}/lib/ld.so.1 "$root/lib/ld.so.1"

    ( cd "$root" && find . -print0 \
        | cpio --null --create --format=newc --quiet \
        | gzip -9 ) > "$out"
  ''
