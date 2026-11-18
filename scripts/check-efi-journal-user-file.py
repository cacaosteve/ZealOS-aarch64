#!/usr/bin/env python3
"""Cold-boot a guest-created file after host-side journal rollback."""
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
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='journal-guest-file-') as name:
    temp = pathlib.Path(name)
    disk, var, test = temp / 'disk.img', temp / 'vars.fd', temp / 'test'
    subprocess.run(['cc', '-std=c11', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_journal_test.c'),
                    '-o', str(test)], check=True)
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        for phase in ('before', 'after'):
            if phase == 'after':
                subprocess.run([str(test), str(disk), 'stage', '6'], check=True)
                subprocess.run([str(test), str(disk), 'recover', '0'], check=True)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            log = ROOT / f'build/utm-parity-goal/journal-user-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, None), log)
            try:
                check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'before':
                    guest.command('zput Tests/JournalKeep.ZC I64 JournalKeep(){return 731;}',
                                  'zput: saved Tests/JournalKeep.ZC')
                else:
                    guest.command('zload disk:Tests/JournalKeep.ZC', 'zc: loaded disk:Tests/JournalKeep.ZC')
                    guest.command('zcall JournalKeep', 'zc => 0x00000000000002db')
            finally:
                guest.close()
        disk.unlink()
        var.unlink()
        print(f'PASS {label}: guest-created file survived host-side recovery and cold boot', flush=True)
