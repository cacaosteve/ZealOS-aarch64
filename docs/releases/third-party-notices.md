# ARM64 preview third-party notices and provenance

These notices identify the materials in the preview images. The accompanying
`licenses/` and `sources/` directories contain the license texts and source
snapshots collected for this candidate. The image was built from project commit
`4a94c23130cf78bb446618f037ce1306af820cde`.

## Pi 4B firmware and device trees

Every regular file from the official [pftf/RPi4 v1.53 firmware archive](https://github.com/pftf/RPi4/releases/tag/v1.53)
was extracted from the staged Pi image and compared byte for byte with the
downloaded archive. All 16 files match. The downloaded ZIP has SHA-256
`ca9973e2a7a546b3df871cfb7382e656829114b6dfa424f40dc67cc90a217d88`.
The firmware release tag points to pftf/RPi4 commit
`ab5d6c98fcf9c38a95b11ea862e701d9c27d5653`.

- `RPI_EFI.fd` is the pftf v1.53 binary. The release build recipe identifies
  EDK2 submodule commits `2970e5699ba6267f3384ffab20f96647578aebc8` (edk2),
  `c8295e076affae5cf04fd4234db13a0e025f44e1` (edk2-platforms), and
  `7ac12d81e02b323bffdf1ef3c188ea33c2185c91` (edk2-non-osi). The pftf
  release describes EDK2's BSD-2-Clause-Patent license; individual components
  have their own terms. Pinned source archives, first-level submodules and
  extracted license files accompany this candidate.
- The three `bcm2711-*.dtb` files match Raspberry Pi firmware commit
  [`b49983637106e5fb33e2ae60d8c15a53187541e4`](https://github.com/raspberrypi/firmware/commit/b49983637106e5fb33e2ae60d8c15a53187541e4),
  explicitly pinned by the v1.53 build recipe. That snapshot's `extra/git_hash`
  identifies Raspberry Pi Linux commit
  [`4ce59edc8d949410d322873b35767e95a5ce5822`](https://github.com/raspberrypi/linux/commit/4ce59edc8d949410d322873b35767e95a5ce5822),
  Linux 6.1.28+. The source snapshot and license texts are included. The
  [firmware README](https://github.com/raspberrypi/firmware/blob/master/README.md)
  describes device-tree files as Linux-derived and GPL-licensed.
- Both `miniuart-bt.dtbo` and `upstream-pi4.dtbo` were independently rebuilt
  from the Linux `4ce59edc8d949410d322873b35767e95a5ce5822` overlay source
  with clang 23.1.5 and DTC 1.8.1; each resulting binary is byte-for-byte
  identical to the v1.53 file. The upstream recipe used a moving branch, so
  this proves a matching corresponding source, not the historical build SHA.
  See `provenance-verification.log` and `scripts/verify-preview-third-party.py`.
- `start4.elf` and `fixup4.dat` match Raspberry Pi firmware commit
  `eef9c230a0e70e23a54be98f90d5237ab6e0b0fd` and its earlier commit
  `3d301dd924bcd758a4c8cb19fe8531031f033f43`. The applicable Raspberry Pi
  Broadcom boot-file license is included; these are distributed as binaries.
- The wireless firmware is from the v1.53 archive. Its README identifies the
  `firmware-brcm80211_20190114-1+rpt11_all.deb` package; the accompanying
  Cypress license and original firmware README are included.

## ZealOS, Aiwnios and Limine

- The ZealOS source snapshot and Unlicense text are in `upstream/pinned` and
  `licenses/ZealOS.txt`. The project's ARM64 source snapshot is included in
  `sources/zealos-aarch64-4a94c231.tar.gz`.
- Aiwnios code is pinned to commit `e155e87a4a4ddae4cd7f25685d9db8869fd45a40`.
  Attribute the project to nrootconauto and contributors; the repository's
  history records nrootconauto as author of the license's introduction in
  commit `05cbed69dfe869cde1083662045f0625be666551`. The exact upstream
  BSD-3-Clause text and our changes are included. The license referred to an
  "above copyright notice" from its first commit, but none was included.
  Historical authorship is not a verified substitute for an absent copyright
  statement; clarification from the project or rights holders remains needed.
- Limine 12.9.0 is included in both images. Its BSD-2-Clause license,
  `3RDPARTY.md`, dependency licenses, upstream source archive and the
  `limine-12.9.0-qemu-virt-pl011-hhdm.patch` accompany this candidate.
  Building that source plus patch with Homebrew clang and LLD 23.1.5 produced
  SHA-256 `3f51768c5eb8a0818b8e7aa2ac9a9caef3c64707b5b6db15d1e612de23236b8c`.
  It matches the Pi EFI executable, the ISO's EFI executable and the sole EFI
  executable embedded in its CD boot image byte for byte. A freshly generated
  CD FAT image differs in its filesystem layout and timestamps; its executable
  matches. The provenance check does not claim identical FAT image bytes.

`SHA256SOURCES` records the staged source archive hashes. Source archive links,
component revisions, and coverage limitations are listed in `SOURCES.md`.
The checksums are integrity checks, not signatures. Do not publish the candidate
until the missing Aiwnios copyright attribution has been clarified and the notices
have been reviewed for the intended distribution.
