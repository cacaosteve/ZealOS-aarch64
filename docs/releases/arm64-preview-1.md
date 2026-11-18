# ZealOS ARM64 preview 1

This is an experimental ARM64 desktop and runtime based on pinned ZealOS
sources. It is not the original ZealOS kernel and does not yet provide full
ZealOS compatibility. It is intended for testing, not irreplaceable data.

## Downloads

- `zealos-arm64-preview-1-utm.iso`: installer and live image for Apple Silicon
  UTM/QEMU with one PCI VirtIO disk.
- `zealos-arm64-preview-1-pi4b.img`: 1 GiB whole-card image for Raspberry Pi
  4B. Write it to a microSD card of at least 1 GiB; flashing erases the card.
- `SHA256SUMS`: checksums for the two images. A matching checksum verifies the
  download against this file; it is not a cryptographic signature of its source.
- The accompanying third-party notices and license files are required when
  distributing the images. Review the firmware source and redistribution
  checklist before publication; see `third-party-notices.md`.

## UTM

Create an ARM64 QEMU VM with a blank 1 GiB VirtIO disk and boot the ISO. Choose
**Y** at the install prompt and confirm the target disk. A fresh install erases
that disk. After installation and verification, power off, detach the ISO, and
start the installed disk. To upgrade, reattach and boot the ISO, choose install,
then select **Upgrade** when an existing compatible installation is detected.
Back up any files you care about before upgrading. Multiple PCI VirtIO targets
are rejected; the installer is intended for one target disk.

## Pi 4B

Write the Pi image to a microSD card, insert it into a Pi 4B, connect a display,
keyboard and mouse, and power it on. Writable SD storage is enabled at startup
when its validation succeeds; no separate installer or `psdopen` is needed in
the normal case. The previous Pi image passed a physical cold-reboot save test;
the user also reported the latest image's Pi test was good. Exact individual
steps of that latter report were not separately recorded.

## Tested and known limits

- Manual UTM tests covered installation, disk-only boot, an upgrade, a user file
  surviving reboot, and two Titanium exits with a clear screen. Automated QEMU
  tests also cover directory growth and interrupted-write recovery on disposable
  128 MiB and 1 GiB disks.
- All 70 pinned `/Apps` and 206 Demo source files compiled in isolated tests;
  selected programs were run interactively. That is not full feature parity.
  The broader individual source inventory had 147 blocked probes out of 340;
  missing dependencies, internal compiler interfaces and x86-specific code
  require individual investigation.
- Automatic directory growth is available on the supported UTM disk layout but
  not on Pi. Pi file moves use copy and delete, and are not atomic. Retain
  backups; interrupted file operations are not covered by a general-purpose
  filesystem journal.
- Pi storage, input, graphics and audio depend on physical hardware and have
  narrower coverage than the emulated environment. Additional peripherals,
  programs and workflows may not work.
- The source-catalog reset path can produce false-negative normal-loader
  results; fresh-guest retries passed for the two affected app loaders.

See [acceptance records](../acceptance/) and [Pi storage status](../PI4-WRITABLE-STORAGE.md)
for the scope of individual results. The old Titanium artifact was observed in
a prior build; the owner-based cleanup has since passed automated and manual
UTM tests, but intermittent faults cannot be ruled out completely.
