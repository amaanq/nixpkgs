{
  lib,
  stdenv,
  fetchFromGitHub,
  fetchpatch,
  enableStatic ? stdenv.hostPlatform.isStatic,
  enableShared ? !enableStatic,
  # Multi-threading with OpenMP is disabled by default
  # more info on https://www.cryptopp.com/wiki/OpenMP
  withOpenMP ? false,
  llvmPackages,
}:

stdenv.mkDerivation rec {
  pname = "crypto++";
  version = "8.9.0";
  underscoredVersion = lib.strings.replaceStrings [ "." ] [ "_" ] version;

  src = fetchFromGitHub {
    owner = "weidai11";
    repo = "cryptopp";
    rev = "CRYPTOPP_${underscoredVersion}";
    hash = "sha256-HV+afSFkiXdy840JbHBTR8lLL0GMwsN3QdwaoQmicpQ=";
  };

  outputs = [
    "out"
    "dev"
  ];

  patches = [
    (fetchpatch {
      name = "CVE-2023-50980-prerequisite.patch";
      url = "https://github.com/weidai11/cryptopp/commit/eb383b8e1622c07da2d5d6599a8b0e17a0deee0f.patch";
      hash = "sha256-Su+W6mKIm94/Ov+l4apPOCG5+R8l5JTfoL/CnVsGykc=";
    })
    (fetchpatch {
      name = "CVE-2023-50980.patch";
      url = "https://github.com/weidai11/cryptopp/commit/641ae35258de397774744b8b17ef6632c3fa48b3.patch";
      hash = "sha256-LloNJfhMBZtMg6WgFyE36ZmmU6cFSHj50FJvu1UIY8k=";
    })
    (fetchpatch {
      name = "CVE-2023-50981.patch";
      url = "https://github.com/weidai11/cryptopp/commit/9aa07aebbdc62461c3ac32f958d7c3ab89ff6f73.patch";
      hash = "sha256-0Z8s+nDg+uy5+3cc1PraLKKPyOFMMe78ybvrf4YmQtU=";
    })
  ];

  postPatch = ''
    substituteInPlace GNUmakefile \
      --replace "AR = /usr/bin/libtool" "AR = ar" \
      --replace "ARFLAGS = -static -o" "ARFLAGS = -cru"
  '';

  buildInputs = lib.optionals (stdenv.cc.isClang && withOpenMP) [ llvmPackages.openmp ];

  makeFlags = [ "PREFIX=${placeholder "out"}" ];

  buildFlags =
    lib.optional enableStatic "static" ++ lib.optional enableShared "shared" ++ [ "libcryptopp.pc" ];

  enableParallelBuilding = true;
  hardeningDisable = [ "fortify" ];

  env = lib.optionalAttrs withOpenMP {
    CXXFLAGS = "-fopenmp";
  };

  doCheck = true;

  # always built for checks but install static lib only when necessary
  preInstall = lib.optionalString (!enableStatic) "rm -f libcryptopp.a";

  installTargets = [ "install-lib" ];
  installFlags = [ "LDCONF=true" ];

  meta = {
    description = "Free C++ class library of cryptographic schemes";
    homepage = "https://cryptopp.com/";
    changelog = [
      "https://raw.githubusercontent.com/weidai11/cryptopp/CRYPTOPP_${underscoredVersion}/History.txt"
      "https://github.com/weidai11/cryptopp/releases/tag/CRYPTOPP_${underscoredVersion}"
    ];
    license = with lib.licenses; [
      boost
      publicDomain
    ];
    platforms = lib.platforms.all;
    maintainers = [ ];
  };
}
