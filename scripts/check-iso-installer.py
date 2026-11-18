#!/usr/bin/env python3
"""Install the ISO image onto a disposable QEMU disk and verify reboot persistence."""
import argparse
import hashlib
import os
import pathlib
import select
import shlex
import shutil
import subprocess
import tempfile
import time
import struct
import zlib
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
DISK_BYTES = 128 * 1024 * 1024


def file_digest(path: pathlib.Path) -> bytes:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.digest()


class Guest:
    def __init__(self, command: list[str], log_path: pathlib.Path):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, bufsize=0)
        self.qmp_path = pathlib.Path(command[command.index("-qmp") + 1].split(",")[0][5:])
        self.buffer = bytearray()
        self.log = log_path.open("wb")

    def until(self, marker: bytes, timeout: int = 120) -> bytes:
        start = time.monotonic()
        while marker not in self.buffer:
            if self.process.poll() is not None:
                raise RuntimeError("QEMU exited before prompt:\n" +
                                   self.buffer[-4000:].decode(errors="replace"))
            if time.monotonic() - start > timeout:
                raise RuntimeError(f"timed out waiting for {marker!r}:\n" +
                                   self.buffer[-4000:].decode(errors="replace"))
            if select.select([self.process.stdout], [], [], 0.2)[0]:
                data = os.read(self.process.stdout.fileno(), 65536)
                if not data:
                    raise RuntimeError("QEMU serial output closed")
                self.buffer.extend(data)
                self.log.write(data)
                self.log.flush()
        end = self.buffer.index(marker) + len(marker)
        result = bytes(self.buffer[:end])
        del self.buffer[:end]
        return result

    def command(self, line: str, expected: str, timeout: int = 180) -> bytes:
        result = bytearray()
        for byte in line.encode():
            self.process.stdin.write(bytes([byte]))
            self.process.stdin.flush()
            result.extend(self.until(bytes([byte]), timeout))
        self.process.stdin.write(b"\r")
        self.process.stdin.flush()
        result.extend(self.until(b"\n> ", timeout))
        if expected.encode() not in result:
            raise RuntimeError(f"{line}: missing {expected!r}:\n" +
                               result.decode(errors="replace"))
        print(f"PASS {line}: {expected}", flush=True)
        return bytes(result)

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                from installer_input import qmp
                qmp(self.qmp_path, "quit")
            except (OSError, ValueError, RuntimeError):
                self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.close()


def build_command(temp: pathlib.Path, vars_file: pathlib.Path,
                  target: pathlib.Path, iso: pathlib.Path | None,
                  usb_cd: bool = False) -> list[str]:
    fw = os.environ.get("FW_CODE", "/opt/homebrew/share/qemu/edk2-aarch64-code.fd")
    platform = shlex.split(os.environ.get(
        "QEMU_VIRT", "-machine virt,accel=hvf,gic-version=3 -cpu host -m 1024 "
        "-device ramfb -device virtio-keyboard-device -device virtio-tablet-device"))
    command = [os.environ.get("QEMU", "qemu-system-aarch64")] + platform
    command += ["-drive", f"if=pflash,unit=0,format=raw,file={fw},readonly=on",
                "-drive", f"if=pflash,unit=1,format=raw,file={vars_file}",
                "-serial", "stdio", "-monitor", "none", "-display", "none",
                "-qmp", f"unix:{temp / 'qmp.sock'},server=on,wait=off"]
    command += ["-audiodev", f"wav,id=acceptance_audio,path={temp / 'audio.wav'}",
                "-device", "virtio-sound-pci,audiodev=acceptance_audio"]
    if usb_cd:
        # Exercise UTM's NEC controller, USB installer media and native input
        # together, retaining VirtIO devices as recovery input on every boot.
        command += ["-device", "nec-usb-xhci,id=installer_usb",
                    "-device", "usb-kbd,bus=installer_usb.0,port=1",
                    "-device", "usb-tablet,bus=installer_usb.0,port=2"]
    if iso:
        command += ["-drive", f"if=none,format=raw,file={target},id=target",
                    "-device", "virtio-blk-pci,drive=target,bootindex=1",
                    "-drive", f"if=none,file={iso},id=cd,media=cdrom,readonly=on"]
        if usb_cd:
            command += ["-device", "usb-storage,bus=installer_usb.0,port=3,drive=cd,bootindex=0"]
        else:
            command += ["-device", "virtio-scsi-pci,id=scsi",
                        "-device", "scsi-cd,drive=cd,bootindex=0"]
    else:
        command += ["-drive", f"if=none,format=raw,file={target},id=target",
                    "-device", "virtio-blk-pci,drive=target,bootindex=0"]
    return command


def boot_to_prompt(guest: Guest, stage: str) -> bytes:
    start = time.monotonic()
    # Cold source compilation can slow substantially on a busy development Mac.
    boot = guest.until(b"\n> ", timeout=int(os.environ.get("ZEAL_ACCEPT_BOOT_TIMEOUT", "900")))
    required = ("zc: native compiler ready", "zc: source Window Manager started at boot",
                "zc: graphical terminal started at boot; serial shell remains available")
    text = boot.decode(errors="replace")
    if "virtio-blk: ready (read-only probe)" not in text and "virtio-blk: rw OK" not in text:
        raise RuntimeError(f"{stage}: no successful VirtIO block probe")
    for marker in required:
        if marker not in text:
            raise RuntimeError(f"{stage} boot missing {marker!r}:\n{text[-5000:]}")
    if "uncaught guest exception" in text or "failed=1" in text:
        raise RuntimeError(f"{stage}: guest runtime failed despite startup markers")
    if "kernel: no usable memory for image" in text:
        raise RuntimeError("ISO modules exhausted the guest's usable memory")
    print(f"PASS {stage} boot and startup desktop markers ({time.monotonic()-start:.1f}s)",
          flush=True)
    return boot


def compare_files(expected: pathlib.Path, actual: pathlib.Path) -> None:
    if expected.stat().st_size != actual.stat().st_size:
        raise RuntimeError("installed disk capacity differs from the source image")
    expected_hash, actual_hash = file_digest(expected), file_digest(actual)
    if expected_hash != actual_hash:
        raise RuntimeError(f"installed disk differs: {expected_hash.hex()} != {actual_hash.hex()}")
    print(f"PASS installed raw disk matches byte-for-byte: sha256={actual_hash.hex()}",
          flush=True)


def check_expanded_disk(source: pathlib.Path, target: pathlib.Path) -> None:
    """Independently check GPT CRCs, boundaries, EFI bytes and relocated seeds."""
    sectors = target.stat().st_size // 512
    with source.open("rb") as src, target.open("rb") as dst:
        def get(stream, lba, count=1):
            stream.seek(lba * 512)
            return stream.read(count * 512)
        primary = get(dst, 1)
        backup = get(dst, sectors - 1)
        for header, here, there in ((primary, 1, sectors-1), (backup, sectors-1, 1)):
            assert header[:8] == b"EFI PART"
            h = bytearray(header[:92]); crc = struct.unpack_from("<I", h, 16)[0]
            h[16:20] = bytes(4)
            assert zlib.crc32(h) == crc
            assert struct.unpack_from("<QQ", header, 24) == (here, there)
            table_lba = struct.unpack_from("<Q", header, 72)[0]
            table = get(dst, table_lba, 32)
            assert zlib.crc32(table) == struct.unpack_from("<I", header, 88)[0]
        entries = get(dst, 2, 32)
        assert entries == get(dst, sectors-33, 32)
        first, last = struct.unpack_from("<QQ", entries, 128+32)
        assert first == 130911 and last == sectors-34
        assert get(src, 2048, 130911-2048) == get(dst, 2048, 130911-2048)
        assert get(src, 131039, 131072) == get(dst, sectors-33-131072, 131072)
        boot = get(dst, first)
        assert boot[3] == 0x88 and boot[510:] == b"\x55\xaa"
        assert struct.unpack_from("<Q", boot, 16)[0] == last-first+1-131072
    print("PASS expanded GPT CRCs, full-capacity data partition, EFI and source seeds", flush=True)


def legacy_files(path):
    result = {}
    with path.open("rb") as stream:
        candidates = []
        for base in (131039, 196575):
            stream.seek(base*512)
            header = stream.read(512)
            if header[:8] == b"ZCSRC001":
                candidates.append((0, base))
            elif header[:8] == b"ZCSRC002" and zlib.crc32(header[:20]) == struct.unpack_from("<I", header, 20)[0]:
                candidates.append((struct.unpack_from("<I", header, 16)[0], base))
        if not candidates:
            raise RuntimeError("existing image has no source bank")
        _, base = max(candidates, key=lambda item: item[0])
        pos = 1
        while pos < 65536:
            stream.seek((base+pos)*512)
            header = stream.read(512)
            if header == bytes(512):
                break
            if header[:8] not in (b"ZCFILE1\0", b"ZCDEL1\0\0"):
                raise RuntimeError("invalid existing record")
            length, crc, blocks, name_len = struct.unpack_from("<IIII", header, 8)
            if not 0 < name_len <= 95 or pos + 1 + blocks > 65536:
                raise RuntimeError("invalid existing record bounds")
            name = header[32:32+name_len].decode("ascii")
            if header[:8] == b"ZCDEL1\0\0":
                result.pop(name, None)
            else:
                data = stream.read(length)
                if zlib.crc32(data) != crc:
                    raise RuntimeError("existing file CRC mismatch: " + name)
                result[name] = data
            pos += 1+blocks
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", nargs="?", type=pathlib.Path,
                        default=ROOT / "build/zealos-aarch64-hello.iso")
    parser.add_argument("--usb-cd", action="store_true",
                        help="use NEC xHCI USB CD, keyboard and tablet alongside VirtIO recovery input")
    parser.add_argument("--source-disk", type=pathlib.Path,
                        default=ROOT / "build/disk.img",
                        help="disk image packaged in the ISO, for byte-for-byte comparison")
    parser.add_argument("--log-prefix", default="check-iso-installer")
    parser.add_argument("--editor", action="store_true", help="type/save/reopen through the graphical editor")
    parser.add_argument("--stock-editor", action="store_true", help="exercise Ed(\"...\") and verify its document across reboot")
    parser.add_argument("--guided", action="store_true", help="exercise the graphical terminal installer using QMP keys")
    parser.add_argument("--target-mib", type=int, default=128)
    parser.add_argument("--output-disk", type=pathlib.Path,
                        help="save the verified disposable installed disk for further testing")
    parser.add_argument("--existing-disk", type=pathlib.Path,
                        help="boot and test a disposable copy; never install over the supplied image")
    parser.add_argument("--existing-kernel", type=pathlib.Path,
                        help="replace only the kernel in the disposable existing-disk copy")
    args = parser.parse_args()
    if args.existing_kernel and not args.existing_disk:
        parser.error("--existing-kernel requires --existing-disk")
    if args.target_mib < 128:
        parser.error("target must be at least 128 MiB")
    iso = args.iso
    if not iso.is_absolute():
        iso = ROOT / iso
    source_disk = args.source_disk
    if not source_disk.is_absolute():
        source_disk = ROOT / source_disk
    if not iso.exists() or not source_disk.exists():
        raise SystemExit(f"missing {iso} or {source_disk}; run `make iso` first")
    if not args.existing_disk:
        print("ISO SHA256 " + file_digest(iso).hex(), flush=True)
        print("Template SHA256 " + file_digest(source_disk).hex(), flush=True)
    fw_vars = pathlib.Path(os.environ.get("FW_VARS_IN", "/opt/homebrew/share/qemu/edk2-arm-vars.fd"))
    with tempfile.TemporaryDirectory(prefix="zeal-iso-install-") as name:
        temp = pathlib.Path(name)
        target = temp / "target.img"
        target.touch()
        with target.open("r+b") as stream:
            stream.truncate(args.target_mib * 1024 * 1024)
        vars_file = temp / "vars.fd"
        shutil.copyfile(fw_vars, vars_file)

        if args.existing_disk:
            if args.existing_disk.stat().st_size != DISK_BYTES:
                raise RuntimeError("existing compatibility fixture must be 128 MiB raw")
            shutil.copyfile(args.existing_disk, target)
            existing_files = legacy_files(target)
            if "Tests/InstallerPersist.ZC" in existing_files:
                raise RuntimeError("fixture already contains test file; use a snapshot without Tests/InstallerPersist.ZC")
            if args.existing_kernel:
                # Upgrade only the disposable clone's EFI payload; prove the
                # legacy partition table and all non-EFI bytes stay intact.
                before = target.read_bytes()
                subprocess.run(["mcopy", "-o", "-i", str(target) + "@@1048576",
                                str(args.existing_kernel.resolve()),
                                "::/boot/kernel.elf"], check=True)
                after = target.read_bytes()
                if before[:1048576] != after[:1048576] or before[130911*512:] != after[130911*512:]:
                    raise RuntimeError("kernel replacement changed non-EFI bytes")
                extracted = temp / "checked-kernel.elf"
                subprocess.run(["mcopy", "-i", str(target) + "@@1048576",
                                "::/boot/kernel.elf", str(extracted)], check=True)
                if file_digest(extracted) != file_digest(args.existing_kernel):
                    raise RuntimeError("kernel replacement read-back mismatch")
                print("PASS disposable legacy kernel replacement SHA256 " +
                      file_digest(extracted).hex() + "; all non-EFI bytes unchanged", flush=True)
            print("Testing disposable copy of existing install: " + str(args.existing_disk), flush=True)
        else:
            live = Guest(build_command(temp, vars_file, target, iso, args.usb_cd),
                         ROOT / f"build/{args.log_prefix}-live.log")
            try:
                boot = boot_to_prompt(live, "ISO live")
                if args.guided and b"hc IR OK (front+host)" in boot:
                    raise RuntimeError("normal ISO ran boot regression tests")
                untouched_hash = file_digest(target)
                live.command("zinstall", "Type zinstall ERASE to confirm.")
                if file_digest(target) != untouched_hash:
                    raise RuntimeError("the unconfirmed zinstall command changed the target")
                if args.guided:
                    from installer_input import stage, text, qmp
                    def startup_prompt(guest):
                        guest.command("zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}",
                                      "zc: executed")
                        stage(guest, 5)

                    startup_prompt(live)
                    qmp(live.qmp_path, "screendump", {"filename": str(ROOT / f"build/{args.log_prefix}-prompt.ppm")})
                    text(live.qmp_path, "n")
                    stage(live, 0)
                    if file_digest(target) != untouched_hash:
                        raise RuntimeError("declining the ISO prompt changed the target")
                    print("PASS No at the ISO startup prompt leaves the target unchanged", flush=True)
                    live.close()
                    shutil.copyfile(fw_vars, vars_file)
                    live = Guest(build_command(temp, vars_file, target, iso, args.usb_cd),
                                 ROOT / f"build/{args.log_prefix}-live-install.log")
                    boot_to_prompt(live, "ISO live install")
                    startup_prompt(live)
                    text(live.qmp_path, "y")
                    stage(live, 2)
                    if file_digest(target) != untouched_hash:
                        raise RuntimeError("the installer wrote before disk confirmation")
                    qmp(live.qmp_path, "screendump", {"filename": str(ROOT / f"build/{args.log_prefix}-confirm.ppm")})
                    text(live.qmp_path, "n")
                    stage(live, -1)
                    if file_digest(target) != untouched_hash:
                        raise RuntimeError("declining disk confirmation changed the target")
                    print("PASS No at disk confirmation leaves the target unchanged", flush=True)
                    live.close()
                    shutil.copyfile(fw_vars, vars_file)
                    live = Guest(build_command(temp, vars_file, target, iso, args.usb_cd),
                                 ROOT / f"build/{args.log_prefix}-live-confirm.log")
                    boot_to_prompt(live, "ISO live confirmation")
                    startup_prompt(live)
                    text(live.qmp_path, "y")
                    stage(live, 2)
                    text(live.qmp_path, "y")
                    stage(live, 6, timeout=600)
                    qmp(temp / "qmp.sock", "screendump", {"filename": str(ROOT / f"build/{args.log_prefix}-complete.ppm")})
                    # The affirmative key-down can power off the VM before a
                    # second QMP request has a chance to release the key.
                    qmp(live.qmp_path, "send-key", {
                        "keys": [{"type": "qcode", "data": "y"}]})
                    try:
                        live.process.wait(timeout=20)
                    except subprocess.TimeoutExpired as exc:
                        raise RuntimeError("Reboot Now did not power off the VM") from exc
                    output, _ = live.process.communicate()
                    live.log.write(output)
                    if live.process.returncode or b"Installer: powering off after verified installation" not in output:
                        raise RuntimeError("VM exited without the verified installer power-off marker")
                    print("PASS Reboot Now powers off after full verification", flush=True)
                    installed = b"graphical install verified"
                else:
                    installed = live.command("zinstall ERASE",
                                             "installed and fully read-back verified.", timeout=600)
                if "erase failed" in installed.decode(errors="replace") or \
                   "compare failed" in installed.decode(errors="replace"):
                    raise RuntimeError("installer reported an error")
            finally:
                live.close()

            if args.target_mib == 128:
                compare_files(source_disk, target)
            else:
                check_expanded_disk(source_disk, target)
        # Reset UEFI's volatile boot choice; the target is the only bootable disk.
        shutil.copyfile(fw_vars, vars_file)
        installed_guest = Guest(build_command(temp, vars_file, target, None, args.usb_cd),
                                ROOT / f"build/{args.log_prefix}-disk-boot.log")
        try:
            boot = boot_to_prompt(installed_guest, "installed disk")
            if args.guided and b"hc IR OK (front+host)" in boot:
                raise RuntimeError("normal installed disk ran boot regression tests")
            status = installed_guest.command("zstatus", "desktop: winmgr=1 gterm=1")
            if "presented_frames=" not in status.decode(errors="replace"):
                raise RuntimeError("installed disk did not start the graphical desktop")
            if not args.existing_disk:
                installed_guest.command(
                    'zc BootstrapGuiTerminalRunLightProgram('
                    '"/Demo/Games/Stadium/StadiumGen.ZC");',
                    "zc: executed")
                deadline = time.monotonic() + 30
                while True:
                    status = installed_guest.command("zstatus", "guest_task[")
                    if "name=StadiumGen" not in status.decode(errors="replace"):
                        break
                    if time.monotonic() >= deadline:
                        raise RuntimeError("StadiumGen GUI task did not return after 30 seconds")
                    time.sleep(0.25)
                installed_guest.command(
                    'zc Print("stadium_gen_readable=%d\\n", '
                    '!!GRRead("/Demo/Games/Stadium/StadiumBG.GR"));',
                    "stadium_gen_readable=1")
                installed_guest.command(
                    'zc Print("stadium_gen_arithmetic=%d\\n", 40+2);',
                    "stadium_gen_arithmetic=42")
            if args.target_mib > 128:
                installed_guest.command(
                    'zc U8 *big=MAlloc(1048576); MemSet(big,90,1048576); '
                    'Print("large_write=%d\\n",FileWrite("/Home/Large.BIN",big,1048576)>0); Free(big);',
                    "large_write=1")
            if args.editor or args.stock_editor:
                from installer_input import editor
                editor(installed_guest, stock_ed=args.stock_editor)
            installed_guest.command('zc Sound(60);', 'zc: executed')
            time.sleep(0.5)  # let the normal shell/device pump feed PCM
            installed_guest.command('zc Sound(0);', 'zc: executed')
            installed_guest.command(
                "zput Tests/InstallerPersist.ZC I64 InstallerPersist(){return 40+2;}",
                "zput: saved Tests/InstallerPersist.ZC")
            installed_guest.command("zload disk:Tests/InstallerPersist.ZC",
                                   "zc: loaded disk:Tests/InstallerPersist.ZC")
            installed_guest.command("zcall InstallerPersist",
                                    "zc => 0x000000000000002a")
        finally:
            installed_guest.close()
        capture = ROOT / f"build/{args.log_prefix}-audio.wav"
        raw = bytearray((temp / "audio.wav").read_bytes())
        # QEMU's WAV backend can leave streaming RIFF/data sizes at zero even
        # after QMP quit. Finalize only that exact PCM header, using actual
        # captured bytes; never synthesize samples or accept an empty stream.
        if (len(raw) >= 44 and raw[:4] == b"RIFF" and raw[8:16] == b"WAVEfmt "
                and raw[36:40] == b"data" and raw[4:8] == bytes(4)
                and raw[40:44] == bytes(4)):
            struct.pack_into("<I", raw, 4, len(raw)-8)
            struct.pack_into("<I", raw, 40, len(raw)-44)
        capture.write_bytes(raw)
        with wave.open(str(capture), "rb") as audio:
            samples = audio.readframes(audio.getnframes())
            if audio.getsampwidth() != 2 or not any(samples):
                raise RuntimeError("VirtIO sound produced no nonzero PCM samples")
        print("PASS VirtIO sound generated captured PCM audio", flush=True)

        shutil.copyfile(fw_vars, vars_file)
        rebooted = Guest(build_command(temp, vars_file, target, None, args.usb_cd),
                         ROOT / f"build/{args.log_prefix}-reboot.log")
        try:
            boot_to_prompt(rebooted, "rebooted installed disk")
            rebooted.command("zstatus", "desktop: winmgr=1 gterm=1")
            rebooted.command("zload disk:Tests/InstallerPersist.ZC",
                             "zc: loaded disk:Tests/InstallerPersist.ZC")
            rebooted.command("zcall InstallerPersist", "zc => 0x000000000000002a")
            if args.editor or args.stock_editor:
                from installer_input import editor
                editor(rebooted, reopen=True, stock_ed=args.stock_editor)
            if args.target_mib > 128:
                rebooted.command(
                    'zc I64 sz,i,bad=0; U8 *big=FileRead("/Home/Large.BIN",&sz); '
                    'if(big)for(i=0;i<sz;i++)if(big[i]!=90)bad++; '
                    'Print("large_size=%d bad=%d\\n",sz,bad); Free(big);',
                    "large_size=1048576 bad=0")

        finally:
            rebooted.close()
        if args.existing_disk:
            after = legacy_files(target)
            for name, contents in existing_files.items():
                if after.get(name) != contents:
                    raise RuntimeError("existing source file changed: " + name)
            print(f"PASS all {len(existing_files)} existing source files preserved byte-for-byte", flush=True)
        if args.output_disk:
            shutil.copyfile(target, args.output_disk)
            print(f"PASS saved verified disposable disk: {args.output_disk}", flush=True)
    media = "NEC USB CD" if args.usb_cd else "SCSI CD"
    print(f"PASS {media} ISO installer → disposable VirtIO disk → rebooted desktop and saved data",
          flush=True)


if __name__ == "__main__":
    main()
