#!/usr/bin/env python3
"""Exercise the diagnostic-only guest journal on disposable disk copies."""
import argparse
import importlib.util
import pathlib
import shutil
import subprocess
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
with tempfile.TemporaryDirectory(prefix='guest-efi-journal-') as name:
    temp = pathlib.Path(name)
    disk, var, esp = temp / 'disk.img', temp / 'vars.fd', temp / 'esp.img'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        for phase in ('setup', 'interrupt', 'recover', 'verify'):
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = args.iso.resolve() if phase in ('interrupt', 'recover') else None
            log = ROOT / f'build/utm-parity-goal/guest-journal-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/GuestJournal.ZC I64 GuestJournal(){return 947;}',
                                  'zput: saved Tests/GuestJournal.ZC')
                elif phase == 'interrupt':
                    guest.command('zinstall journal-test interrupt', 'diagnostic interruption staged', timeout=300)
                elif phase == 'recover':
                    guest.command('zinstall journal-test recover', 'diagnostic recovery complete', timeout=300)
                else:
                    guest.command('zload disk:Tests/GuestJournal.ZC', 'zc: loaded disk:Tests/GuestJournal.ZC')
                    guest.command('zcall GuestJournal', 'zc => 0x00000000000003b3')
            finally:
                guest.close()
        with disk.open('rb') as src, esp.open('wb') as dst:
            src.seek(1048576)
            dst.write(src.read(128863 * 512))
        subprocess.run(['fsck.fat', '-n', str(esp)], check=True, stdout=subprocess.DEVNULL)
        print(f'PASS {label}: guest recovery and user file after cold boot', flush=True)
        disk.unlink()
        var.unlink()
        esp.unlink()
