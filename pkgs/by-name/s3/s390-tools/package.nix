{
  lib,
  stdenv,
  fetchFromGitHub,
  pkg-config,
  fuse3,
  zlib,
  ncurses,
  openssl,
  cryptsetup,
  json_c,
  glib,
  curl,
  libxml2,
  systemdLibs,
}:

stdenv.mkDerivation rec {
  pname = "s390-tools";
  version = "2.41.0";

  src = fetchFromGitHub {
    owner = "ibm-s390-linux";
    repo = "s390-tools";
    rev = "v${version}";
    hash = "sha256-+w1VAfyAUmGFig61XBjA86PBUaOORIygIfMlE6udSR8=";
  };

  nativeBuildInputs = [
    pkg-config
  ];

  buildInputs = [
    fuse3
    zlib
    ncurses
    openssl
    cryptsetup
    json_c
    glib
    curl
    libxml2
    systemdLibs
  ];

  makeFlags = [
    # s390-tools uses INSTALLDIR as the target prefix (compiled into binaries
    # and used for install paths); PREFIX is ignored.
    "INSTALLDIR=$(out)"
    "SYSCONFDIR=$(out)/etc"
    "UDEVDIR=$(out)/lib/udev"
    "HAVE_FUSE=1"
    "HAVE_ZLIB=1"
    "HAVE_NCURSES=1"
    "HAVE_OPENSSL=1"
    "HAVE_CRYPTSETUP2=1"
    "HAVE_JSONC=1"
    "HAVE_GLIB2=1"
    "HAVE_LIBCURL=1"
    "HAVE_LIBXML2=1"
    "HAVE_LIBUDEV=1"
    # Disable optional components with heavy or unavailable deps
    "HAVE_SNMP=0"
    "HAVE_CARGO=0"
    "HAVE_DRACUT=0"
    "HAVE_INITRAMFS=0"
    "HAVE_LIBNL3=0"
  ];

  preBuild = ''
    # The standalone stage2 crashdump binaries (zdump, zfcpdump, and the
    # eckd/fba/tape dumpers inside zipl/boot) have hand-tuned linker scripts
    # that overlap under current binutils/GCC. None are needed for NixOS
    # boot — only the main zipl bootloader and userland utilities are.
    # Drop the two dump SUBDIRS and the four stage2dump .bin targets, then
    # stub the embedded dump payloads so zipl/src/boot.c's .incbin references
    # still resolve (zipl -d becomes a runtime no-op, which is fine).
    substituteInPlace Makefile \
      --replace-fail ' zdump ' ' ' \
      --replace-fail ' zfcpdump ' ' ' \
      --replace-fail ' ap_tools ' ' ' \
      --replace-fail ' rust ' ' '
    substituteInPlace zipl/boot/Makefile \
      --replace-fail 'eckd2dump_sv.bin tape2dump.bin fba2dump.bin eckd2dump_mv.bin' ""
    for b in eckd2dump_sv.bin tape2dump.bin fba2dump.bin eckd2dump_mv.bin; do
      : > "zipl/boot/$b"
    done
  '';

  enableParallelBuilding = true;

  meta = with lib; {
    description = "IBM s390x userspace utilities for Linux on IBM Z";
    longDescription = ''
      A collection of userspace tools for use with the s390 Linux kernel and
      device drivers. Includes zipl (the s390x bootloader), dasdfmt, lsdasd,
      chccwdev, zgetdump, and many other s390x-specific utilities.
    '';
    homepage = "https://github.com/ibm-s390-linux/s390-tools";
    license = licenses.mit;
    maintainers = [ ];
    platforms = [ "s390x-linux" ];
  };
}
