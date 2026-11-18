# ARM64 upgrade recovery diagnostic

The normal installer now offers Upgrade on recognized ARM64 installations.
The diagnostic tests below exercise the underlying recovery protocol.

Disposable 128 MiB and 1 GiB QEMU installs passed a combined EFI commit and
boot-time source-bank publication, preserving a user-created file, a modified
source file and a deleted source file. The same disk sizes passed an interrupted
EFI switch, ISO journal recovery, cancellation, a new attempt and cold-boot file
checks. Cancellation publishes redundant cancellation records rather than
clearing the last copy, so retry can recognize a torn final record.

The native staging test injected torn writes at the first and second cancellation
records (1, 7, 16, 31, 52 and 511 bytes), as well as an earlier journal write.
It verifies that cancellation refuses a modified active source bank and that a
new attempt can prepare and publish after recovery.

The graphical Upgrade choice was tested on disposable 128 MiB and 1 GiB
installations through three consecutive upgrades, including an ISO with an
additional source file. Saved files, a file created between upgrades, a modified
source and a deletion survived each reboot. FAT consistency checks passed.
The candidate ISO is `build/utm-parity-goal/repeat-upgrade-candidate.iso`,
SHA-256 `ee3ed1c6c34bd9b12c9292504a07a233e29aff98d1b85c3466c35f0ead0f95d7`.
On both disk sizes, guest fault injection covered reclamation, a torn source
marker, a torn slot-fingerprint record, and the boot-configuration switch;
normal ISO recovery and retry preserved the tested files. Unknown and modified
inactive EFI files were left intact and blocked the upgrade. Fresh installation,
boot, and save after reboot also passed against the ISO's extracted template.

The compressed baseline can now be decoded as a bounded stream and compared
read-only against the installed source bank. Guest tests on 128 MiB and 1 GiB
disks preserve a new file, an edited source and a deletion. Host tests compare
the full archive manifest and reject a damaged archive. Repeat upgrades use the
active slot's compressed baseline and reclaim space before staging the alternate
slot.

A read-only ownership scan now rejects shared FAT clusters and cycles before
reclamation. Disposable copies of both layouts passed valid, cross-link and
cycle checks. The original archive occupies 88,600 clusters and spans 693 FAT
sectors: updating both mirrors plus its directory entry needs 1,387 journal
records, exceeding the 650-record journal. An experimental cursor now reclaims
the archive over multiple journal transactions. Host tests on disposable 128 MiB
and 1 GiB copies interrupt initial cursor publication, first-batch and later-batch
writes, recover, check FAT consistency and freed space, verify the compressed
archive and hash all data outside EFI and the reserved journal. The guest
reclaim starts only after verifying the upgraded boot configuration and archive;
the normal ISO recovery path resumes an interrupted cursor. Slot fingerprints
prevent cleanup of unknown or modified inactive EFI files. These automated
QEMU checks do not cover every possible interruption or storage failure.

On 2026-11-16, Steve reported a manual UTM test using the candidate ISO and a
fresh 1 GiB VirtIO disk: installation and persistence after reboot passed;
both the first and second upgrades completed, and `Hello.TXT` retained its text
after booting the upgraded disk each time. During the first upgrade, the main
window remained blank until the completion/reboot prompt. This is an open
progress-feedback issue, not a failure of the reported persistence test.

A subsequent installer build splits the upgrade into visible preparation, source
file, and boot-file stages and yields between them. Automated QEMU screen
captures showed the progress text during upgrades on both 128 MiB and 1 GiB
disks. On 2026-11-17, Steve confirmed the third upgrade in UTM on the same
1 GiB disk: progress messages were visible, and Hello.TXT opened with its
existing text after booting the upgraded disk.
