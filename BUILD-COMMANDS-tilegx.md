# TILE-Gx nixpkgs cross — build commands (run by the user)

Phase B wired the tilegx toolchain into this nixpkgs checkout
(`/home/amaanq/projects/nix/nixpkgs/tilegx`, bookmark `tilegx-cross`). The
expressions evaluate and the dry-run plan is the correct cross bootstrap. These
are the commands to actually realize it. **Run them yourself** — they are heavy
builds (gcc + glibc cross), and this host has an OOM/bad-RAM history, so keep the
job count modest.

All commands run from the workspace root:

```
cd /home/amaanq/projects/nix/nixpkgs/tilegx
```

## 0. Sanity (cheap, already verified — re-run if you want)

```
# platform elaborates
nix-instantiate --eval --strict --json --expr \
  'let p = import ./. { crossSystem = (import ./lib).systems.examples.tilegx; }; in { isTile = p.stdenv.hostPlatform.isTile; linuxArch = p.stdenv.hostPlatform.linuxArch; }'
# expect: {"isTile":true,"linuxArch":"tile"}

# dry-run plan for hello (no build)
nix build --dry-run .#pkgsCross.tilegx.hello
# expect 14 derivations: binutils -> nolibc-gcc -> linux-headers-4.16 ->
#   glibc-nolibgcc -> libgcc -> glibc -> gcc -> wrappers -> hello
```

## 1. Build the cross toolchain (the keystone)

Order is enforced by the dependency graph; you only need to ask for the top of
each layer. Keep `--cores`/`-j` modest (gcc + glibc are the RAM-heavy steps).

```
# kernel headers (fast, pulls linux-4.16.tar.xz ~99 MiB)
nix build -j1 --cores 8 .#pkgsCross.tilegx.linuxHeaders

# binutils (stock 2.46, tilegx is upstream — no patch)
nix build -j1 --cores 8 .#pkgsCross.tilegx.buildPackages.binutils

# the cross gcc (this is the big one — applies the tilegx backend patch,
# builds cc1/cc1plus + libgcc + libstdc++). EXPECT 30-90 min and high RAM.
nix build -j1 --cores 6 .#pkgsCross.tilegx.buildPackages.gcc

# glibc 2.42 for tilegx (applies the tile sysdeps patch)
nix build -j1 --cores 8 .#pkgsCross.tilegx.glibc

# the whole cross stdenv (ties it together)
nix build -j1 --cores 6 .#pkgsCross.tilegx.stdenv
```

If you prefer one shot, just build hello (step 2) — Nix realizes the toolchain
as dependencies. Building the layers explicitly only helps you localize a
failure.

## 2. Build hello — the first end-to-end TILE-Gx ELF

```
nix build -j1 --cores 6 .#pkgsCross.tilegx.hello
```

### What success looks like

```
file result/bin/hello
# => ELF 64-bit LSB executable, Tilera TILE-Gx ..., (dynamically|statically) linked
```

`readelf -h result/bin/hello` should report `Machine: Tilera TILE-Gx` and
little-endian (`2's complement, little endian`). A dynamically-linked binary will
have interpreter `/lib/ld.so.1` (or `/lib/ld-linux-tile.so.*`) and bind
`GLIBC_2.34` — proving it resolved against the new 2.42 libc.

## Job-count / resource guidance

- `--cores 6` to `8` per derivation, `-j1` (one derivation at a time) — this host
  has a bad-RAM/OOM history; do NOT crank `-j` or `--cores` high. If gcc OOMs,
  drop to `--cores 4`.
- The gcc step is the long pole (it builds the cross compiler + libgcc +
  libstdc++ from the 44k-line backend patch). Budget tens of minutes and watch
  memory; `dmesg | tail` after a mysterious failure to check for the OOM killer.
- Block on each build in the foreground; do not background them.

## Gotchas to watch on the real build

- **gcc OOM**, not logic: the most likely failure is memory pressure during the
  gcc build, not a patch failure (the patch is purely additive on tilegx files +
  untouched config files, and was confirmed in the dry-run derivation). Lower
  `--cores` and retry before suspecting the port.
- The make-4.4 glibc hang from the standalone port is **NOT** a risk here —
  nixpkgs glibc is 2.42 (modern), and that hang was glibc-2.27-only.
- The 4.16 tile uapi lacks ~47 post-4.16 syscalls glibc 2.42 references; the
  tile sysdeps patch already adds their asm-generic numbers to arch-syscall.h,
  and glibc's runtime ENOSYS fallbacks cover them. No action needed at build
  time, but it is why the headers are pinned old.
- libstdc++ in the cross gcc is rebuilt against the in-tree glibc 2.42, so the
  toolchain is internally consistent (unlike the standalone port whose libstdc++
  was built vs the old 2.27 sysroot).
- On-hardware execution is unverified (the board's rshim was off-bus during the
  port); a successful `nix build` proves cross-compilation + linking, not yet a
  run on silicon.

## Provenance of the injected patches

- gcc: `pkgs/development/compilers/gcc/patches/tilegx-backend.patch`
  regenerated from `nlrrpymr` in
  `/home/amaanq/projects/forks/gcc-tilegx-port/gcc-15.2.0` (includes the
  native-TLS configure arm).
- glibc: `pkgs/development/libraries/glibc/tilegx-sysdeps.patch` =
  the tile-only additive diff of commits `slwwuksz..yvvoulku` in
  `/home/amaanq/projects/forks/glibc` (README + build-many-glibcs.py excluded as
  unnecessary and conflict-prone).
