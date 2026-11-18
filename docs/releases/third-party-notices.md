# Third-party material for the ARM64 preview

The images contain or build upon third-party software. Do not publish the Pi
image until the exact included firmware and device-tree source versions and
their applicable distribution obligations have been checked.

- ZealOS sources are pinned in `upstream/pinned`; their accompanying license is
  `upstream/pinned/LICENSE`.
- Aiwnios is pinned by `third_party/aiwnios/UPSTREAM.json`; its license is
  `third_party/aiwnios/LICENSE`, and local modifications are documented in
  `third_party/aiwnios/PORT.patch`. The binary distribution must carry the
  applicable license and copyright notices.
- Limine boot binaries are included in both images. Preserve the license and
  third-party notices for the exact Limine binary version and its modifications.
- The Pi image is assembled using pftf/RPi4 firmware v1.53. Its `Readme.md`
  identifies `RPI_EFI.fd` with the EDK2 BSD-2-Clause-Patent license, identifies
  Raspberry Pi boot files separately, and identifies the wireless blobs under
  `firmware/` with the Cypress license included in that directory.
- The Raspberry Pi firmware project's README identifies `start*.elf` and
  `fixup*.dat` under the Broadcom boot license, and describes the device trees
  and overlays as built from Linux kernel sources. Confirm the matching source
  revision and GPL distribution requirements for the precise supplied files.

The release bundle should include the complete relevant license texts and
notices, not just this summary. The upstream references are:

- https://github.com/pftf/RPi4/releases/tag/v1.53
- https://github.com/tianocore/edk2/blob/master/License.txt
- https://github.com/raspberrypi/firmware/blob/master/README.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.x/3RDPARTY.md
