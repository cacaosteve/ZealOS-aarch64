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
- The `miniuart-bt.dtbo` and `upstream-pi4.dtbo` overlays match the above
  firmware snapshot and also later snapshots. The v1.53 recipe fetched overlays
  from `master`, so byte matches alone do **not** establish the exact source
  revision used to produce those overlay binaries. For traceability the source
  bundle includes the Linux revision above and the revisions recorded by two
  later matching firmware snapshots: `89586905b8603e545cce9089a81f5f35d65bc998`
  and `16f1da3c4e94437449d6aa151589ca0ad4b388bb`. The latter is the most recent
  firmware commit's recorded Linux revision before the release build, but is
  not proof of the overlay build's exact source. Confirm the build provenance
  with the publisher before representing it as established.
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
- Aiwnios code is pinned to commit `e155e87a4a4ddae4cd7f25685d9db8869fd45a40`;
  the exact upstream BSD-3-Clause license and local changes are included in
  the project source snapshot. The upstream license refers to an "above
  copyright notice", but its pinned LICENSE has no such notice. Do not invent
  an attribution; resolve the missing notice with upstream before representing
  this package as a completed copyright-notice review.
- Limine 12.9.0 is included in both images. Its BSD-2-Clause license,
  `3RDPARTY.md`, dependency license files, upstream source archive and the
  repository's `limine-12.9.0-qemu-virt-pl011-hhdm.patch` accompany this
  candidate. The staged `BOOTAA64.EFI` and `limine-uefi-cd.bin` differ from
  the stock files in the local Limine binary archive. The patch applies to
  the local 12.9.0 source, but a build matching these exact staged binaries has
  not been established. Verify their build provenance and any other changes
  before treating the included source and notices as complete for publication.

`SHA256SOURCES` records the staged source archive hashes. Source archive links,
component revisions, and coverage limitations are listed in `SOURCES.md`.
The checksums are integrity checks, not signatures. Do not publish the candidate
until the remaining overlay, Aiwnios attribution, and Limine provenance questions
have been resolved and the notices reviewed for the intended distribution.
