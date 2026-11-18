# UTM ARM64 parity check — 2026-11-15

Tested ISO: `build/utm-parity-goal/parity-filemgr-fix.iso`
SHA-256: `2cb34616f6626e7fe4846a7377d3d791c4aa376dcb8bd2bc875d5b10b325f0a3`

The automated QEMU ARM64 checks used disposable disk copies. The guided USB ISO test declined installation and declined disk erasure without changing the target, then installed to a blank 1 GiB VirtIO disk, checked the expanded GPT, booted without the ISO, and reopened a graphical-editor save after a cold reboot. A 1 MiB binary survived with every byte checked. A copy of an existing 128 MiB installation booted with the new kernel, preserved 888 source files, and retained a newly saved file after reboot. This is a kernel replacement test, not an in-guest upgrade test.

All 14 groups in `build/utm-parity-goal/filemgr-regression/results.log` passed. All 70 pinned Apps `.ZC` files and all 206 pinned Demo `.ZC` files compiled in the contexts described by `build/utm-parity-goal/apps-filemgr-catalog.json` and `build/utm-parity-goal/demo-filemgr-catalog.json`; this is compile evidence, not proof of every runtime behavior. The nine grouped app probes in `build/utm-parity-goal/filemgr-app-results.log` passed, covering the 12 top-level Apps. Span saved and reopened an edited bridge after a cold boot; Budget also passed cold persistence. Psalmody generated non-silent audio samples, though audible quality requires a live check. An edited header was used by the compiler before and after a cold reboot in `build/utm-parity-goal/filemgr-header-cold-check.log`.

A broader, supplementary system-source scan was stopped after the required Apps inventory completed. Its partial log records known x86-only SSE and network-driver cases and code using compiler-private APIs; it is not a complete system-source inventory.

Stock x86 installation offers an in-guest Upgrade or Install choice. The ARM64 ISO currently offers fresh installation with a disk-capacity and destructive-write confirmation; it has no equivalent in-guest upgrade. Reboot Now powers off so the ISO can be detached in UTM. The automated display comparison also observed a different guest resolution. Live UTM review is still needed for perceived flicker, splash dismissal, colors/status behavior, and audible sound.
