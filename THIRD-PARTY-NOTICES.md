# License and third-party notices

The root [MIT license](LICENSE) applies to original ZealOS AArch64 project
contributions that the contributors have the right to license. It does not
replace the licenses or notices for third-party material. Files that incorporate
or adapt third-party work remain subject to the applicable upstream terms;
MIT licensing of original contributions does not remove those obligations.

- **ZealOS:** The pinned upstream source in [`upstream/pinned/`](upstream/pinned/)
  and other incorporated or adapted ZealOS material are governed by the
  [upstream Unlicense](upstream/pinned/LICENSE). The pinned revision is recorded
  in [`upstream/pinned/UPSTREAM.json`](upstream/pinned/UPSTREAM.json).
- **Aiwnios:** The imported compiler in [`third_party/aiwnios/`](third_party/aiwnios/)
  is distributed under its [BSD-3-Clause license](third_party/aiwnios/LICENSE).
  The upstream revision and our changes are recorded in
  [`UPSTREAM.json`](third_party/aiwnios/UPSTREAM.json) and
  [`PORT.patch`](third_party/aiwnios/PORT.patch). The upstream license refers
  to an above copyright notice that is absent from the upstream file; see the
  [preview notices](docs/releases/third-party-notices.md) for the attribution
  and the remaining ambiguity.
- **YDE:** Some USB code identifies itself as adapted from YDE public-domain
  drivers; see the comments in [`src/usb_hid.h`](src/usb_hid.h) and
  [`src/usb_xhci.h`](src/usb_xhci.h). Those comments and any applicable upstream
  terms remain with the adapted code.
- **Limine:** The bootloader is a separate BSD-2-Clause project with additional
  third-party components under their respective licenses. The release contains
  its license texts, upstream source and the local patch. See
  [Limine's license](https://github.com/Limine-Bootloader/Limine/blob/v12.9.0/COPYING)
  and [component notices](https://github.com/Limine-Bootloader/Limine/blob/v12.9.0/3RDPARTY.md).
- **Raspberry Pi firmware and device trees:** Pi images include EDK2-based UEFI
  firmware, Raspberry Pi boot firmware, device trees, and wireless firmware with
  distinct licenses. They are not licensed by the project's MIT license. See the
  [Pi firmware project](https://github.com/pftf/RPi4) and the detailed
  [preview notices](docs/releases/third-party-notices.md).

The [ARM64 preview release](https://github.com/cacaosteve/ZealOS-aarch64/releases/tag/v0.1.0-arm64-preview.1)
includes a source-and-notices archive with license texts and source snapshots.
The source and licensing scope of third-party material remain subject to review;
this file is a guide to included notices, not a blanket license for every file.
