# ZealOS aarch64 bring-up

## Download the ARM64 preview

**[Download ZealOS ARM64 Preview 1](https://github.com/cacaosteve/ZealOS-aarch64/releases/tag/v0.1.0-arm64-preview.1)** — no build required.

- **UTM on Apple Silicon:** [Download the installer ISO](https://github.com/cacaosteve/ZealOS-aarch64/releases/download/v0.1.0-arm64-preview.1/zealos-arm64-preview-1-utm.iso). Boot it in an ARM64 VM with a blank VirtIO disk.
- **Raspberry Pi 4B:** [Download the microSD image](https://github.com/cacaosteve/ZealOS-aarch64/releases/download/v0.1.0-arm64-preview.1/zealos-arm64-preview-1-pi4b.img). Write it to a microSD card; writing erases the card.
- **Checksums, source, and instructions:** see the [release page](https://github.com/cacaosteve/ZealOS-aarch64/releases/tag/v0.1.0-arm64-preview.1) and its **Assets** section.

This is an experimental preview. Back up files you want to keep.

Limine → ZealBooter → kernel + `demo.hcbc` → HolyC-IR JIT → graphical terminal → **native USB or VirtIO input**

**Not a full ZealOS port yet.** QEMU/UTM has an accepted ARM64 ISO install,
disk-only desktop boot and saved-data persistence milestone. Physical Pi 4B
UEFI boots the native graphical terminals with working USB keyboard, pointer,
menu clicks and `40+2;` returning `42`. The user has also confirmed Pi audio
through HDMI and the headphone jack, though those listening tests were not tied
to a recorded image hash. The current 1 GiB Pi SD image passed a physical
editor save and cold-reopen test. See
[ACCEPTANCE.md](ACCEPTANCE.md) and [PI4.md](PI4.md).

## AArch64 desktop in UTM

The splash screen, desktop, and Titanium running in UTM:

**Splash screen**

![ZealOS ARM64 splash screen in UTM](docs/images/zealos-arm64-utm-splash.png)

**Desktop**

![ZealOS ARM64 desktop in UTM](docs/images/zealos-arm64-utm-desktop.png)

**Titanium**

![Titanium running on ZealOS ARM64 in UTM](docs/images/zealos-arm64-utm-titanium.png)

## Regular ZealOS programs

The current ISO (`build/zealos-aarch64-hello.iso`, SHA-256
`55719ef8…`) contains the pinned Apps and Demo sources. Source-catalog runs
report all 70 pinned Apps files and all 206 Demo files compile in the ARM64
guest. Runtime acceptance is broader but incomplete: 202 Demo sources have
explicit runtime acceptance, four remain partial, and named-app testing still
has feature and live-UTM gaps. The user has confirmed audible UTM output for
`Beep(60);` and Psalmody on the current image. See
[PROGRAMS.md](PROGRAMS.md) for per-program evidence and remaining work.

The Apps menu includes the tested upstream programs such as Digital Logic,
Budget, GrModels, KeepAway, Psalmody, Span, Strut, TimeClock and Titanium.
Automated UTM-bundled QEMU checks cover these apps. Live UTM checks now confirm
GrModels displays its generated model, and KeepAway accepts pointer input and
returns cleanly with Escape. Other live-window reports remain open. Additional
stock graphics programs including Doodle, Mass Spring and Game of Life have live
UTM acceptance.
Budget's full entry workflow was rerun on the exact current ISO: create, edit,
abort, cancel, save, reopen, cold disk-only persistence, and return to a working
terminal all pass with `zstatus failed=0`. The user also confirmed that launching
Budget in UTM and pressing Escape returns responsively to a terminal. Account-file
editing and account selection remain unverified. See
[`Budget current-image acceptance`](docs/acceptance/budget-current-55719-utm-qemu-2026-11-11.json).
`make check-stock-programs` and `make check-stock-interactive` exercise
drawing, mouse/keyboard input, animation and clean exit in QEMU. Evidence and
remaining compatibility limits are tracked in [PROGRAMS.md](PROGRAMS.md) and
[ACCEPTANCE.md](ACCEPTANCE.md).

## Native source compatibility milestone

A new **persistent compiler session** runs the pinned Aiwnios ARM64 compiler
inside the freestanding guest. The first integration target is the complete,
unchanged upstream `Kernel/QuickSort.ZC`, with separate callers exercising
recursion, callbacks, shared symbols, F64 and reclaimable allocations.

```sh
make check-compat       # disposable ISO guest + old boot regressions
make check-compat-pci   # disposable UTM-shaped GPT/PCI disk guest
make check-gui-terminal # terminal output ownership, menus, paint and zoom/pan
make check-gui-workflow # visible typing/cursors + graphical edit/save/run/reboot
make check-stock-programs # unchanged upstream graphics programs through Apps
make check-stock-interactive # Doodle, Mass Spring and Life with real input
make check-logic-program # original Digital Logic, prompt editing and recovery
make check-budget-program # Budget forms, save/abort and cold-boot persistence
make check-program-catalog # compile-only inventory of all Apps entry points
make check-iso-installer-usb # USB CD install, disk-only boot and persistence
python3 scripts/check-iso-installer.py --usb-cd --target-mib 1024 --guided --editor
python3 scripts/check-installer-guards.py # undersized/multiple targets stay unchanged
# Upgrade only the kernel in a disposable copy of an existing raw installation:
python3 scripts/check-iso-installer.py --usb-cd --existing-disk /path/to/snapshot.img --existing-kernel build/kernel.elf
make check-usb-input    # native NEC/QEMU xHCI keyboard, mouse/tablet + recovery
make check-usb-topology # nested USB2 hubs, repeated reconnects + subtree recovery
make check-pi4-platform # Pi FDT/DMA/xHCI, HID/storage and framebuffer adapter
make check-task-settings # upstream application state + task-local directories
make check-source-context # directory masks + relative document/project files
make check-source-execute # upstream ExeFile + native task-local file execution
make check-source-editor # full-screen editor scanout + save/reopen/run
make check-compiler-recovery # malformed source, statement rollback and task/stack recovery
make check-compiler-context # compiler queues, option inheritance and task isolation
make check-run-file     # task-local LastFun + upstream RunFile with application arguments
make check-source-exceptions # catchable source/file errors + prompt, job and task cleanup
make check-system-launch # upstream Sys/SysFile + popup application arguments
make check-system-server # dedicated source System loop + namespace/job ownership
make check-user-command # original ExeCmdLine loop, options and error recovery
make check-user-startup # per-user HomeSys setup and independent graphical terminals
make check-home-once    # original Once include, boot-media branch, tips and prompt recovery
make check-home-autostart # disk boot launches the saved Home/Once.ZC in the GUI terminal
make check-autocomplete # boot indexing and visible GUI fill-in on a disposable disk
make check-user-text    # User startup commands + XTalk input delivery and recovery
make check-home-path    # shared home paths, includes and saved source after reboot
make check-registry     # DolDoc execution, shared registry and Once across reboot
make check-input-edges  # visible modifier typing, held keys and explicit release
make utm               # refresh VM, preserving RedSea/source edits and adding new bundled files
```

The UTM refresh also updates the repository-managed
`Tests/WindowDragLive.ZC`, `System/GuiTerminal.ZC`, `System/TaskBridge.ZC`,
`System/UserTask.ZC`, `System/HomeStartUp.ZC`, `System/SourceDir.ZC`, `System/SourceHome.ZC`,
`System/VersionScalars.ZC`, `System/Registry.ZC`, `System/SourceRegistry.ZC`,
`System/SourcePrompt.ZC`, `System/SourceTraining.ZC`,
`System/DolDoc/DocCompiler.ZC`, `System/DolDoc/DocTree.ZC`,
`System/DolDoc/DocClipBoard.ZC`, `System/DolDoc/DocFile.ZC`,
`System/DolDoc/DocTerm.ZC`, `System/DolDoc/DocEd.ZC`, `System/WinMgr.ZC`,
`System/Gr/GrDC.ZC`, `System/Gr/SpriteRaster.ZC`, and
`Tests/GrSpriteSymmetry.ZC` files
so they stay in sync with the kernel; other existing guest source files remain
untouched.

The kernel now initializes native xHCI input on QEMU virt (UTM's NEC model
and QEMU xHCI). Keep UTM's normal USB 3.0 setting. A successfully enumerated
USB keyboard and pointer take ownership; VirtIO devices remain configured
as recovery input, with their transports parked while USB is active and
reinitialized on disconnect/failure. Terminal 1
serial input stays independent. `zstatus` reports the selected owners, raw
USB events, controller state, errors, and each device's route including USB2
transaction-translator slot/port; serial `usbcheck` exercises command and
event ring wraparound. This USB backend supports boot keyboards and single
HID mouse/tablet input reports, with or without a report-ID prefix, on root
ports or behind USB 1.x/2.0 hubs.
Port-change polling debounces new connections, removes hub subtrees, and
reuses reserved per-device DMA only after successful slot teardown. Limits
are eight devices, fifteen ports per hub and five hub tiers. A SuperSpeed hub
control path is present, but stock QEMU cannot exercise it and physical SS-hub
validation remains open. The QEMU hub stress gate uses generic full-speed hub
devices; physical high-speed TT routing is also pending. Pi's separate
PCIe/VL805 backend has physical keyboard, basic trackpad and read-only FAT32
USB-storage acceptance, including operation alongside its native desktop.
Writable Pi storage and disconnect/reconnect recovery remain work
([PI4.md](PI4.md)).

At the UTM prompt, run `zcheck`. Expected: `zc: upstream QuickSort + persistent
modules OK`. Use `zc` for declarations/statements, `zload` for a source file in the
boot archive, `zcall Function` (bare name, without parentheses or a semicolon)
for a no-argument integer function, and `zreset`
to discard the native session. Existing `hc`/`runzc` commands keep their original behavior.
`zput Name.ZC <source>` saves a short source file on the native 32 MiB source
partition in the 128 MiB UTM/GPT disk (legacy ISO/MMIO uses RedSea);
the `<source>` marker is explanatory syntax—do not type the angle brackets.
For example, `zput Tests/InstallerPersist.ZC I64 InstallerPersist(){return 40+2;}`
saves a callable persistence check.
`zload disk:Name.ZC` compiles it, including after a full reboot. The QEMU gates also
exercise a relative include between two disk files. `zverify` checks the
seeded large upstream files, and `scripts/source-volume.py put` can import a
larger file while the VM is stopped. For a running UTM guest, `zrecv` accepts
larger files as acknowledged serial hex chunks; `scripts/send-zc.py --emit`
produces lines to paste into Terminal 1. `zls` lists live source files and
`zrm <path>` hides one; `zgc` reclaims superseded records in the running guest
using the disk's second source bank. `zedit <path>` opens a guest line editor;
`j`/`k` move the selected line, `[`/`]` page through the file, and `g`/`G`
jump to its first or last line. It also supports `p`, `a`, `i`, `c`, `d`, `wq`
and `q!` commands, as well as numbered-line edits. Saved files load through
`zload disk:<path>`. The graphical editor reads absolute `/...` paths from the
writable source volume before falling back to bundled files, and saves those
absolute paths to the same persistent volume. `FilesFind` returns a merged
view of bundled files, native source files, and readable files in a legacy
RedSea root: saved replacements supply their native size, saved-only files
appear in listings, and tombstoned files are hidden. The legacy adapter lists
root files only and skips the `.` and `..` directory records.
Nested native-only paths also synthesize their parent directories in recursive
directory searches, and deleting the last native file removes those synthesized
directories from the merged view. Serial uploads refresh the same listing cache
as ordinary saves and deletes.
While `zedit` is active, the full-screen source view is composited over the
desktop every frame; enter its line-edit commands in UTM's built-in Terminal 1.
`make check-source-editor` checks visible controls and edits, modified/saved
state, disk reopen, and execution of the saved file.
The complete unchanged upstream
`KernelA.HH` and `KernelB.HH` load as declaration headers in that order.
The boot terminal's OS menu includes Window Demo, Lattice Play, and Stock Play.
Window Demo launches the two-window move/resize fixture; the two games launch
their interactive graphics demos. Each returns to the terminal when stopped
with Escape.
The unchanged `KMathB.ZC` also loads with ARM64 bindings for task-local `Fs`,
bit operations, the counter and a powers-of-ten table. A bounded cooperative
task bridge exposes the default upstream `Spawn` callback signature, `Yield`,
`TaskValidate`, `Suspend`, `IsSuspended`, `Kill`, `BirthWait`, `DeathWait`,
idle/prompt `TaskWait`, bounded `TaskMessage`/`MessagePost`/`MessageGet`
delivery, a per-task inherited-symbol `TaskExe`/`JobsHandler` subset with `MessageScan`
job dispatch, key-aware `MessageScan`/`MessageGet` and `KeyScan`/`KeyGet` over polled
USB/VirtIO/serial input, portable `CharScan`/`CharGet` behavior, and the pinned
`sys_focus_task` pointer for a focused child waiting on a key. It also exposes
CPU-0 `JobQueue` callbacks and `SpawnQueue` task creation. A reserved executive runs queued jobs; four
live app child tasks still have their own stacks and allocation owners.
Desktop boot also starts a separate System task in a reserved slot. Its
`ServerTaskCont` control flow comes from pinned `KTask.ZC`, with job execution
routed through the native compiler/unwind dispatcher. `Sys` and `SysFile`
execute there; the serial shell retains its own root stack and input. System
shares the persistent kernel namespace, has its own guest heap, directory,
queues and compiler-context stack, and drains UI messages as upstream does.
The graphical terminal now runs the original `UserCmdLine`, `UserTaskCont` and
`ExeCmdLine` control flow through a private ARM64 compiler ABI. Type ZealC
directly, for example `40+2;`. `User;` creates another terminal with its own
document, compiler context and history. The default per-user hook retains the
pinned `HomeSys.ZC` welcome-document/window setup, then installs the accepted
graphical input adapter. `User("40+2;\n")` supplies an initial command;
`XTalk` and `XTalkWait` send further input to that terminal's compiler through
the source KeyDev input filter. `ExeDoc` now reads real DolDoc entries through
the native lexer, including quoted formatting, macros and includes. The original
registry tree APIs share a System-owned cache backed by `~/Registry.ZC`.
`Once`, `SysOnce` and explicit `OnceExe` execute and flush saved User/System
branches. On disk, RAM, and DVD boots, the first graphical terminal now
receives the original `Cd; #include "Once";` startup command through `XTalk`.
Boot run-level flags now record the dedicated System task, persistent Registry,
and Home startup only after each phase succeeds; the reboot checks wait for
the graphical terminal to consume its queued `Once` command before asserting
the persisted values and flushes.
The UTM ISO automatically asks **Install onto hard drive (y or n)?**. Press
**N** to try the live session or **Y** to open the installer. You can also open
it later from **OS → Install ZealOS**. The guide displays the PCI VirtIO
target and its capacity, then asks **Erase this disk and install ZealOS?**.
Press **Y** to confirm or **N** to leave the disk unchanged.
The installer accepts a single PCI VirtIO target of **128 MiB or larger**;
multiple attached VirtIO targets are rejected to avoid erasing an ambiguous disk.
It displays write/read-back progress and verifies every sector after flushing.
After success, **Reboot Now** powers off the VM when you press **Y**. Detach
the ISO in UTM, then start the VM from the installed disk. Press **N** to
stay in the live session. The serial interface retains
`zinstall` / `zinstall ERASE` for automation.

The normal ISO is `build/zealos-aarch64-hello.iso`. Use `make iso-diagnostics`
to build `build/zealos-aarch64-diagnostics.iso` with the boot regression and
framebuffer tests enabled. `make check-serial` boots that diagnostic image.

The Y/N installer ISO (SHA-256 `93ff5bed…`) passed manual Apple Silicon UTM
testing on a blank 1 GiB VirtIO disk. Both prompts worked, read-back verification
completed, and the saved file survived a disk-only restart.
[Manual Y/N installer acceptance](docs/acceptance/utm-yn-installer-93ff5bed-2026-11-13.json)

The previous guided installer ISO (SHA-256 `d9ffff52…`) is preserved as
`build/zealos-aarch64-guided-d9ffff52.iso`.
[The larger-disk acceptance record](docs/acceptance/utm-larger-disk-guided-installer-d9ffff52-2026-11-13.json)
records fresh 1 GiB guided installation, fresh 128 MiB installation, and the
new kernel on a copy of an existing 128 MiB installation with all 889 prior
source files preserved. Checks include disk-only desktop boot, representative
app execution, captured VirtIO audio, graphical editor save/reopen, and cold
reboot persistence. These are automated QEMU/HVF checks with UTM-compatible
devices. The user also confirmed installation, disk-only desktop boot and
file persistence after restart in Apple Silicon UTM on 2026-11-13. This exact
image has not been retested on Pi.

For 128 MiB targets, the installer writes the packaged legacy image byte for
byte. Larger targets retain the EFI partition and use the rest of the
disk for a bounded ZealOS data partition, with RedSea followed by the two seeded
32 MiB source banks (one active, one compaction shadow). Additional capacity is
available for RedSea data; the source store remains 32 MiB. The current file provider supports files up to
8 MiB. Existing installations are never expanded or reformatted merely by
booting them. It is separate from the legacy YDE installer, whose
BIOS/AHCI storage path is not ported. Disk/RAM startup also opens a second
terminal and tiles it beside the first, excluding bridge/System tasks from the
window layout. Startup queues autocomplete indexing after `Once`; the
autocomplete corpus is indexed by its background task, and the first prompt is
checked for a visible caret and usable input after startup output. The bundled
ARM64 `ZSplash.ZC` adapter restores the pinned CP437 mesh asset, runs the
upstream rotating splash with the native pointer enabled, and restores terminal
focus when dismissed. Automated scanout checks cover the two-terminal boot,
splash animation and dismissal, popup windows, terminal input and autocomplete
fill-in.
The OS menu's Paint Demo keeps a small retained stroke while the left pointer
button is held, draws a yellow line as the pointer moves, and clears it on
Escape. QEMU checks exercise pointer sampling, gap interpolation, and actual
screen-CDC raster output. A live UTM screenshot now confirms yellow strokes
drawn over the graphical terminal; Paint Demo is an inline mode, not a separate
window. The Escape reset path clears retained strokes and the partial command;
`make check-home-autostart` exercises the reset and next empty paint frame. The
final-image UTM guest passed the automated Escape test; the saved UTM screenshot
shows Paint Demo drawing but does not capture the Escape gesture itself.
The original `Cd; #include "Once";` path now runs with the compiler's `.ZC`
default extension, source-backed `YorN` and `TipOfDay`, and the original tips and
customization documents. `sys_boot_src` comes from the actual Limine module
medium: disk, optical, or TFTP download. The splash asset is restored from its
UTF-8 CP437 source bytes, and the renderer normalizes the 0x100 transform flag
before using its 8-bit Boolean state. The DVD live path is covered by
`make check-dvd-live`; disk/RAM HomeSys startup preserves its formatted boot-time
banner, two-terminal layout, splash and popup flow. `make check-home-once` also
covers the disk and ISO/DVD noninstall branches, including include/tip/error
cleanup. The ARM64 compiler preserves superscript, subscript and normal-script
DolDoc markers inside identifiers and executes inline binary DolDoc tokens,
including payloads with embedded NUL bytes. Startup uses a guarded translation
of the pinned `HomeSys.StartUpTasks` body. `make check-iso-installer` covers the
bounded ISO installer on a disposable VirtIO disk, including byte-for-byte
image verification, disk boot, and saved source persistence across reboot; it
does not port the original YDE BIOS/AHCI installer. `zstatus`
also reports `keyboard_state` modifier and repeat values to diagnose held host
keys separately from guest decoding.
The bridge also maintains the upstream-visible runnable-task and parent/child
task rings, including `TaskFocusNext` selection and child unlink on teardown.
`TaskMathChecks`,
`TaskSwitchChecks`, `SpawnBridgeChecks`, `TaskLifecycleChecks` and
`TaskWaitChecks`, `TaskIdleChecks`, `TaskMessageChecks`, `TaskExeChecks` and
`TaskExeLifecycleChecks`, `TaskExeFocusChecks`, `TaskMessageJobsChecks`, `TaskKeyChecks`,
`TaskCharQueueChecks`, `KernelBitOpsChecks`, `TaskIRQFlagsChecks`, `TaskQueueChecks`,
`TaskFocusKillChecks`, `TaskCompilerScopeChecks`, `TaskAnswerChecks`, `TaskJobQueueChecks` and
`TaskSpawnQueueChecks` exercise
those paths. Most other x86 intrinsics and unresolved externals
remain unavailable. This does not boot the upstream kernel or provide its
preemptive scheduler.

The preserved installer milestone ISO has SHA-256
`52c214c29a9c23d86c94899e8ef2ba94bd4a21231c92ecdd649714708e2bc627`.
`make check-iso-installer` passed against this exact build: guarded erase,
full 128 MiB read-back, disk-only desktop boot, and saved-data persistence
(`InstallerPersist` returned `0x2a` before and after a second cold reboot).
That build's embedded installer disk had SHA-256
`1a4516a67d42aecc7284bfc68e455c5231767360e75a9df021dddb6946d98fde`.
That exact ISO (`52c214c…`) was installed in UTM to a disposable 128 MiB
VirtIO disk and passed full sector read-back. A separate disk-only UTM VM
cold-booted to the desktop; its captured screen shows both terminals and the
centered animated Z. `InstallerPersist` returned `0x2a` before and after a
second cold reboot. The live guest also returned `0x2a` for Ctrl-Alt-F font
dispatch, while the menu, paint Escape cleanup, and startup graphics checks
passed in the automated guest gates. See [ACCEPTANCE.md](ACCEPTANCE.md).

[COMPATIBILITY.md](COMPATIBILITY.md) describes the scope and remaining work.
This does not boot the original upstream kernel and remains a scoped ARM64
desktop/runtime port. It includes a guided UTM/QEMU ISO installer for PCI VirtIO disks of
128 MiB and larger. Pi diagnostics remain a separate path.

## Status

```
virtio-kbd: OK @ 0xa003e00
virtio-tablet: OK @ 0xa003c00
hc IR OK (front+host)
bc: module => 0x2d OK
> bars
bars ok
> stars
stars n=0x78
> circles
circles ok
> bounce
bounce frames=0x24
> paint
paint dots=0x0
```

| Piece | Result |
|--|--|
| Boot + GIC/CNTP + module JIT | OK |
| HolyC (`enum`/`union`, `I64*`/`U8*`, `U32`/`Bool`, `true`/`NULL`, ≤8 fns/≤5 args) | OK |
| Host: strings/`Mem*`/`HashStr`, math, `Cnt`, mouse deltas, keys, gfx | OK |
| Fast `Cls`/`FillRect` + `GrLine`/`GrHLine`/`GrVLine`/`GrCircle`/`GrFillCircle` | OK |
| Shell `bars` / `stars` / `circles` / `bounce` / `paint` | OK |
| virtio keyboard + tablet | OK (QEMU/UTM) |
| native USB keyboard + mouse/tablet | NEC and QEMU xHCI matrices pass USB-only, VirtIO fallback, nested hubs/hotplug; live UTM typing and released-pointer drag accepted |
| RedSea + freeze `runzc` UseAdd/Notes/MemSort | OK (`make check-serial` / `run-pci`; UTM eyes-on in ACCEPTANCE.md) |
| Lattice compose (`disklat` / `latticeplay`) | NEC/xHCI live-control regression green (`make check-lattice-controls`); UTM placement/aim, color/Return restart, Space/Escape, and zoom confirmed |

Window keystrokes (`make run-iso`) and serial both feed the shell. HHDM lacks the virtio-mmio window, so the kernel maps a 2MiB Device block at `0x0a000000` before probing.

`make check-serial` uses QEMU virt + MMIO virtio disk. It does **not** exercise the UTM app window or Pi UART RX. Lattice smokes run a reduced DiskLat-shaped source ([upstream/DiskLat.ZC](upstream/DiskLat.ZC)), not proof that stock ZealOS `Demo/Graphics/Lattice.ZC` runs unchanged.

## Shell

```
help | abs | sum | bars | stars | circles | bounce | paint | halt
hc GrCircle(200,150,40,0xFFFFFF);return 1;
hc I64 x=3;I64 *p=&x;*p=9;return x+(NULL==0);
disklat | latticeplay   # Lattice compose; Esc exits live play
```

## Build

```sh
make && make run-serial
make run-iso   # bars / stars / circles / bounce / paint / click / Esc
make check-serial   # QEMU regression (must print hc IR OK)
```

UTM: `make utm` → only **ZealosAarch64Hello** ([ACCEPTANCE.md](ACCEPTANCE.md)).

Pi 4B UEFI (parallel track): [PI4.md](PI4.md). `make pi-desktop-sd` prepares
`build/pi-desktop-esp/` with the native desktop, bundled sources and the accepted
USB diagnostic recovery entry. `make pi-image PI_FIRMWARE_ROOT=/path/to/copied-pftf-boot`
builds a raw image with a 512 MiB FAT32 firmware/boot partition and an isolated
ZealOS data partition; the image size defaults to 8 GiB and can be changed with
`PI_IMAGE_SIZE_MIB`. It requires `mkfs.fat`, `mcopy`, and `mmd`, and never writes
to a physical card. The current Pi candidate automatically opens a validated
ZealOS SD data partition at startup and enables bounded SDHCI writes; `psdopen`
remains available for retry. Automatic startup and file persistence after a
cold reboot passed a physical Pi 4B test on 2026-11-17
([acceptance record](docs/acceptance/pi4-auto-sd-persistence-2026-11-17.json)). The image builder
creates a full-size RedSea volume with `/Home` and `/Tmp`, and ordinary
`FileWrite` create/replace plus `DirMake` use that volume. `.ZC` source saves
remain in separate source banks. File and bounded directory deletion are
supported. File moves use copy followed by deletion; automatic directory growth
is not implemented. See [the writable-storage status and layout](docs/PI4-WRITABLE-STORAGE.md)
and [the physical editor persistence result](docs/acceptance/pi4-sd-editor-persistence-2026-11-15.json).
A fresh card needs [pftf/RPi4](https://github.com/pftf/RPi4/releases).
`make pi-usb-image PI_FIRMWARE_ROOT=/path/to/copied-pftf-boot` builds a live USB
desktop that can install into a prepared SD data partition while preserving its
FAT boot partition. See [the Pi 4B USB install steps](docs/PI4-WRITABLE-STORAGE.md#pi-4b-live-usb-installer).

Bridge notes: [AIWNIOS.md](AIWNIOS.md). Milestone ladder: [upstream/README.md](upstream/README.md).

## Tracks

| Track | Status |
|--|--|
| QEMU/UTM virt | The preserved `build/zealos-aarch64-logic-prompt-fix.iso` (`9c5e90c9…`) corrects Digital Logic's graphical prompt editing and fatal setup-abort path. Expanded NEC/VirtIO Logic, editor/save/reboot/reopen, Doodle/Mass Spring/Life and USB-CD install/persistence gates pass. The user confirmed the corrected-image live UTM retest works; its screenshot shows the expected AND circuit. The preserved `3b59782c…` image has live UTM acceptance for Doodle, Mass Spring and Life; `52c214c…` has live installer/desktop/persistence acceptance. See [ACCEPTANCE.md](ACCEPTANCE.md) and [PROGRAMS.md](PROGRAMS.md) for the remaining collection. |
| Pi 4B UEFI | **Physical editor save and cold reboot passed on the 1 GiB SD image (2026-11-15).** Native desktop input, physical USB reads and headphone-jack/HDMI0 audio were previously user-confirmed. See [the physical editor persistence result](docs/acceptance/pi4-sd-editor-persistence-2026-11-15.json) and [the writable-storage status](docs/PI4-WRITABLE-STORAGE.md). |

## Earlier platform follow-ups

1. UTM: continue the remaining application compatibility work listed in PROGRAMS.md. Digital Logic's corrected-ISO retest on `9c5e90c9…` is accepted by user report. Doodle, Mass Spring and Life pass live checks in the preserved `3b59782c…` image. The current image passes expanded Logic, interactive-program, editor and ISO install/persistence gates; the broader graphics gates passed before the source-only prompt correction.
2. Pi: extend the accepted native desktop/input milestone to source-program/editor workflows and writable save/reboot/reopen persistence; validate the new SDHCI write path on hardware, then complete normal RedSea integration, disconnect recovery and installation acceptance.
3. HCRT host stubs only after IR gap shrinks; DT-based discovery before other ARM64 boards
