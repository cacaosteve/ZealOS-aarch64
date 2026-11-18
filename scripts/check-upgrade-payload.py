#!/usr/bin/env python3
"""Validate the ISO upgrade payload without modifying disposable targets."""
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
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='upgrade-payload-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        before = check.file_digest(disk)
        shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
        log = ROOT / f'build/utm-parity-goal/payload-{label}.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        guest = check.Guest(check.build_command(temp, var, disk, args.iso.resolve()), log)
        try:
            check.boot_to_prompt(guest, f'{label} ISO payload')
            guest.command('zinstall upgrade-payload-check', 'compressed upgrade archive verified', timeout=300)
        finally:
            guest.close()
        if before != check.file_digest(disk):
            raise RuntimeError('payload validation changed the target')
        print(f'PASS {label}: payload verified; target unchanged', flush=True)
        disk.unlink()
        var.unlink()
