{
  lib,
  stdenv,
  fetchurl,
  fetchpatch,
  pkg-config,
  xorgproto,
  libx11,
  libxext,
  libxfixes,
  writeScript,
  testers,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "libxi";
  version = "1.8.3";

  outputs = [
    "out"
    "dev"
    "man"
    "doc"
  ];

  src = fetchurl {
    url = "mirror://xorg/individual/lib/libXi-${finalAttrs.version}.tar.xz";
    hash = "sha256-etYAVvAa9PeGz+k7OncHRHcRYm/I2iY3vscakECbq+U=";
  };

  # https://gitlab.freedesktop.org/xorg/lib/libxi/-/merge_requests/23
  patches = [
    (fetchpatch {
      name = "CVE-2026-93541.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/22e730b9afe95757ff2440d42a06dfa99fd67bb2.patch";
      hash = "sha256-bwV4H3pTLsYJcfuM78IJ+4SjewfZSt23Y8aGLN/mnzw=";
    })
    (fetchpatch {
      name = "CVE-2026-93542.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/68b5d62787d02d9e4a6ecc3c660c88517ac0174d.patch";
      hash = "sha256-2VzCdhyBIJiCH5qkm9SHIHK5tUI9iu+2SZSCSoqiTmM=";
    })
    (fetchpatch {
      name = "CVE-2026-93543.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/a0fb826e548c8a2bfdf78c40af9de027eb4a8ec6.patch";
      hash = "sha256-MZT78JVQlje2j0KyUeMojml/ufAGGnxQMAW2UGCPgFU=";
    })
    (fetchpatch {
      name = "CVE-2026-93544.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/5629f68275999aaeab9e5dc8fd36443c8da8fcea.patch";
      hash = "sha256-GQJV0OmDDm4OFh7Qd/HOj+MYA7mDdGkz4JLn/TLD0ik=";
    })
    (fetchpatch {
      name = "CVE-2026-93545.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/fd98b166952583b308c30d0e3e16d5cb33d667ff.patch";
      hash = "sha256-OfJc5qDlzQzixBkug7E+Px6jqlHLdFnOejAQMINgzCk=";
    })
    (fetchpatch {
      name = "CVE-2026-94281.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/3d322af0629b292dc8a26e1109eb24b5704fdb3a.patch";
      hash = "sha256-LtnDvm0+YT56DBgXoIMsEepum+UJjkla3/sw9snUVow=";
    })
    (fetchpatch {
      name = "CVE-2026-94282.patch";
      url = "https://gitlab.freedesktop.org/xorg/lib/libxi/-/commit/3af139147be0103d1c6e90df89337a37fb766450.patch";
      hash = "sha256-Ayejuuic91hSkeaUJsomPJDaNvuJXzEKM5IkKeNnE4s=";
    })
  ];

  strictDeps = true;

  nativeBuildInputs = [ pkg-config ];

  buildInputs = [
    xorgproto
    libx11
    libxext
    libxfixes
  ];

  propagatedBuildInputs = [
    xorgproto
    # header file dependencies
    libx11
    libxext
    libxfixes
  ];

  configureFlags =
    lib.optional (stdenv.hostPlatform != stdenv.buildPlatform) "xorg_cv_malloc0_returns_null=no"
    ++ lib.optional stdenv.hostPlatform.isStatic "--disable-shared";

  passthru = {
    updateScript = writeScript "update-${finalAttrs.pname}" ''
      #!/usr/bin/env nix-shell
      #!nix-shell -i bash -p common-updater-scripts
      version="$(list-directory-versions --pname libXi \
        --url https://xorg.freedesktop.org/releases/individual/lib/ \
        | sort -V | tail -n1)"
      update-source-version ${finalAttrs.pname} "$version"
    '';
    tests.pkg-config = testers.testMetaPkgConfig finalAttrs.finalPackage;
  };

  meta = {
    description = "library for the X Input Extension";
    homepage = "https://gitlab.freedesktop.org/xorg/lib/libxi";
    license = with lib.licenses; [
      mitOpenGroup
      hpnd
      mit
    ];
    maintainers = [ ];
    pkgConfigModules = [ "xi" ];
    platforms = lib.platforms.unix;
  };
})
