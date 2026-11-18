#!/usr/bin/env python3
"""Prove rejected installer targets remain unchanged in disposable QEMU guests."""
import importlib.util
import pathlib
import os
import shutil
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("installer_check", ROOT / "scripts/check-iso-installer.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def main():
    for case, mib, message in (("small", 64, "at least 128 MiB"),
                               ("multiple", 128, "multiple targets are ambiguous")):
        with tempfile.TemporaryDirectory(prefix="zeal-install-guard-") as directory:
            temp = pathlib.Path(directory)
            target, extra = temp / "target.img", temp / "extra.img"
            for path in (target, extra):
                with path.open("wb") as stream:
                    stream.truncate(mib * 1024 * 1024)
            original = {path: check.file_digest(path) for path in (target, extra)}
            variables = temp / "vars.fd"
            shutil.copyfile(os.environ.get("FW_VARS_IN", "/opt/homebrew/share/qemu/edk2-arm-vars.fd"), variables)
            iso = pathlib.Path(os.environ.get(
                "ZEAL_TEST_ISO", ROOT / "build/zealos-aarch64-hello.iso"))
            command = check.build_command(temp, variables, target, iso, True)
            if case == "multiple":
                command += ["-drive", f"if=none,id=extra,format=raw,file={extra}",
                            "-device", "virtio-blk-pci,drive=extra"]
            guest = check.Guest(command, ROOT / f"build/installer-guard-{case}.log")
            try:
                check.boot_to_prompt(guest, case)
                for action in ("zinstall", "zinstall ERASE", "zinstall begin ERASE"):
                    guest.command(action, message)
                guest.command("zinstall step", "no active installation")
            finally:
                guest.close()
            for path, digest in original.items():
                if check.file_digest(path) != digest:
                    raise RuntimeError(f"{case}: rejected target changed: {path.name}")
            print(f"PASS {case}: all target bytes unchanged after preview, erase and guided-start attempts", flush=True)


if __name__ == "__main__":
    main()
