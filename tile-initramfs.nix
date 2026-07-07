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
  btop = pkgs.btop;

  # Populate /bin so an interactive shell has real command names on PATH
  # (busybox applets + btop) instead of everyone typing store paths.
  profile = build.writeText "profile" ''
    export PATH=/bin HOME=/root TERM=xterm LANG=C.UTF-8
    alias btop='btop --force-utf'
    stty rows 50 cols 200 2>/dev/null
    echo "tilegx nix rootfs — glibc 2.42, busybox ${busybox.version}, btop on PATH. Type: btop"
  '';

  init = build.writeText "init" ''
    #!${busybox}/bin/sh
    export PATH=${busybox}/bin HOME=/root TERM=xterm LANG=C.UTF-8 ENV=/etc/profile
    busybox mkdir -p /proc /sys /dev /dev/pts /tmp /root /bin /etc
    busybox mount -t proc     proc   /proc
    busybox mount -t sysfs    sys    /sys
    busybox mount -t devtmpfs dev    /dev     2>/dev/null || busybox mdev -s
    busybox mount -t devpts   devpts /dev/pts 2>/dev/null

    busybox --install -s /bin
    busybox ln -sf ${btop}/bin/btop /bin/btop
    busybox cp ${profile} /etc/profile

    echo
    echo ">>>>>> NIX TILE-GX ROOTFS UP (glibc 2.42, busybox ${busybox.version}) <<<<<<"
    busybox uname -a
    ${hello}/bin/hello || echo "  hello FAILED rc=$?"
    echo ">>>>>> interactive shell on hvc0 — type 'btop' <<<<<<"

    # Shell on /dev/console (hvc0). With USE_TMF_CON OFF, hvc0 is the bidirectional
    # rshim console (the one tile-monitor --console drives), so it can be typed at.
    # setsid -c makes hvc0 the controlling tty (job control + full-screen btop).
    while :; do
      busybox setsid -c busybox sh -l </dev/console >/dev/console 2>&1
      busybox sleep 1
    done
  '';

  closure = build.closureInfo { rootPaths = [ busybox hello btop init ]; };
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
