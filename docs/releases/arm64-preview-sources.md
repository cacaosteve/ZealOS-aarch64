# Source and provenance bundle

These upstream snapshots are provided for review and distribution alongside the preview images. GitHub source archives omit Git submodule contents; the separate `edk2-*` dependency archives correspond to the first-level gitlinks in the pinned EDK2 tree. The included Linux source reproduces both overlays byte for byte, and Limine source plus the supplied patch reproduces the EFI executable byte for byte. Recursive dependency completeness and the upstream Aiwnios copyright-notice ambiguity remain documented; see `third-party-notices.md`.

- `pftf-rpi4-ab5d6c98.tar.gz`: https://github.com/pftf/RPi4/tree/ab5d6c98fcf9c38a95b11ea862e701d9c27d5653
- `edk2-2970e569.tar.gz`: https://github.com/tianocore/edk2/tree/2970e5699ba6267f3384ffab20f96647578aebc8
- `edk2-platforms-c8295e07.tar.gz`: https://github.com/tianocore/edk2-platforms/tree/c8295e076affae5cf04fd4234db13a0e025f44e1
- `edk2-non-osi-7ac12d81.tar.gz`: https://github.com/tianocore/edk2-non-osi/tree/7ac12d81e02b323bffdf1ef3c188ea33c2185c91
- `raspberrypi-linux-4ce59edc8d.tar.gz`: https://github.com/raspberrypi/linux/tree/4ce59edc8d949410d322873b35767e95a5ce5822
- `raspberrypi-linux-89586905b8.tar.gz`: https://github.com/raspberrypi/linux/tree/89586905b8603e545cce9089a81f5f35d65bc998
- `raspberrypi-linux-16f1da3c4e.tar.gz`: https://github.com/raspberrypi/linux/tree/16f1da3c4e94437449d6aa151589ca0ad4b388bb
- `limine-12.9.0-upstream.tar.gz`: https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.0
- `RPi4_UEFI_Firmware_v1.53.zip`: https://github.com/pftf/RPi4/releases/tag/v1.53
- `edk2-brotli-e230f474.tar.gz`: https://github.com/google/brotli/tree/e230f474b87134e8c6c85b630084c612057f253e
- `edk2-mbedtls-0bebf8b8.tar.gz`: https://github.com/ARMmbed/mbedtls/tree/0bebf8b8c7f07abe3571ded48a11aa907a1ffb20
- `edk2-openssl-8cf17aae.tar.gz`: https://github.com/openssl/openssl/tree/8cf17aaeb4599f8af87fefd810b5b5fee90fe69e
- `edk2-oniguruma-4ef89209.tar.gz`: https://github.com/kkos/oniguruma/tree/4ef89209a239c1aea328cf13c05a2807e5c146d1
- `edk2-libfdt-cfff8054.tar.gz`: https://github.com/devicetree-org/pylibfdt/tree/cfff805481bdea27f900c32698171286542b8d3c
- `edk2-mipisyst-370b5944.tar.gz`: https://github.com/MIPI-Alliance/public-mipi-sys-t/tree/370b5944c046bab043dd8b133727b2135af7747a
- `edk2-jansson-e9ebfa7e.tar.gz`: https://github.com/akheron/jansson/tree/e9ebfa7e77a6bee77df44e096b100e7131044059
- `edk2-libspdm-f55cf6d4.tar.gz`: https://github.com/DMTF/libspdm/tree/f55cf6d48ec69b4ac60a63903e9c6a2cb0fd155d
- `edk2-tpm-bc29a21d.tar.gz`: https://github.com/TrustedComputingGroup/TPM/tree/bc29a21d44b01396223c152a4834e52318591770
- `edk2-cmocka-1cc9cde3.tar.gz`: https://github.com/tianocore/edk2-cmocka/tree/1cc9cde3448cdd2e000886a26acf1caac2db7cf1
- `edk2-googletest-86add134.tar.gz`: https://github.com/google/googletest/tree/86add13493e5c881d7e4ba77fb91c1f57752b3a4
- `edk2-subhook-83d4e1eb.tar.gz`: https://github.com/tianocore/edk2-subhook/tree/83d4e1ebef3588fae48b69a7352cc21801cb70bc

The project source archive `zealos-aarch64-4a94c231.tar.gz` is the image source commit. The Limine patch applied to the included upstream 12.9.0 source reproduces the staged EFI executable byte for byte with the recorded toolchain. `limine-12.9.0-stock-binaries.tar.gz` and the RPi4 ZIP are binary reference archives, not source.
