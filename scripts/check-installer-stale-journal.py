#!/usr/bin/env python3
"""Verify retired large-journal records do not look like a legacy journal."""
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
from installer_input import text

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('installer_iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(prefix='stale-journal-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        # Sector 1572 is the old journal's shadow header but lies in the
        # 650-slot journal's records. Model a retired, non-header record.
        with disk.open('r+b') as out:
            out.seek((34 + 2 + 3 * 512) * 512)
            out.write(b'stale larger journal record' + bytes(485))
        shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
        log = ROOT / f'build/utm-parity-goal/stale-journal-{label}.log'
        guest = check.Guest(check.build_command(temp, var, disk, args.installer_iso.resolve()), log)
        try:
            check.boot_to_prompt(guest, label)
            guest.until(b'Install onto hard drive', timeout=120)
            text(guest.qmp_path, 'n')
            guest.until(b'Take Tour', timeout=120)
            text(guest.qmp_path, 'n')
            guest.command('zinstall recovery', 'no pending recovery', timeout=300)
        finally:
            guest.close()
        print(f'PASS {label}: retired larger-journal record ignored', flush=True)
