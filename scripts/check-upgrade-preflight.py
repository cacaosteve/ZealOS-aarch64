#!/usr/bin/env python3
"""Check installer boot and read-only upgrade detection on disposable disks."""
import argparse
import importlib.util
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('installer_check', ROOT / 'scripts/check-iso-installer.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=pathlib.Path, required=True)
    parser.add_argument('--legacy', type=pathlib.Path, required=True)
    parser.add_argument('--expanded', type=pathlib.Path, required=True)
    parser.add_argument('--log-dir', type=pathlib.Path, default=ROOT / 'build/upgrade-preflight')
    args = parser.parse_args()
    args.log_dir.mkdir(parents=True, exist_ok=True)
    fixtures = [
        ('legacy', args.legacy, 'recognized 128 MiB legacy installation'),
        ('expanded', args.expanded, 'recognized expanded installation'),
        ('blank', None, 'no recognized existing ARM64 installation'),
    ]
    with tempfile.TemporaryDirectory(prefix='upgrade-preflight-') as name:
        temp = pathlib.Path(name)
        for label, source, expected in fixtures:
            disk, var = temp / (label + '.img'), temp / (label + '-vars.fd')
            if source:
                shutil.copyfile(source, disk)
            else:
                with disk.open('wb') as stream:
                    stream.truncate(128 * 1024 * 1024)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            before = check.file_digest(disk)
            guest = check.Guest(check.build_command(temp, var, disk, args.iso.resolve(), True),
                                args.log_dir / (label + '-serial.log'))
            try:
                check.boot_to_prompt(guest, label)
                guest.command('zinstall upgrade-check', expected, timeout=180)
            finally:
                guest.close()
            if check.file_digest(disk) != before:
                raise RuntimeError(label + ' disk changed during live ISO boot or preflight')
            print('PASS ' + label + ' ISO boot and preflight left the entire disk unchanged', flush=True)


if __name__ == '__main__':
    main()
