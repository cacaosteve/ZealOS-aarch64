#!/usr/bin/env python3
"""Check normal installer recovery on disposable guest-created installs."""
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
from installer_input import stage, text

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('installer_iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='installer-recovery-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        for phase in ('setup', 'interrupt', 'installer', 'verify'):
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = (args.diagnostic_iso.resolve() if phase == 'interrupt' else
                   args.installer_iso.resolve() if phase == 'installer' else None)
            log = ROOT / f'build/utm-parity-goal/normal-recovery-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/RecoveryKeep.ZC I64 RecoveryKeep(){return 983;}',
                                  'zput: saved Tests/RecoveryKeep.ZC')
                elif phase == 'interrupt':
                    guest.command('zinstall journal-test interrupt', 'diagnostic interruption staged', timeout=300)
                elif phase == 'installer':
                    guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}', 'zc: executed')
                    stage(guest, 5)
                    text(guest.qmp_path, 'y')
                    stage(guest, 2)
                    # The normal GUI must already have performed recovery.
                    guest.command('zinstall recovery', 'no pending recovery', timeout=300)
                    text(guest.qmp_path, 'n')
                    stage(guest, -1)
                else:
                    guest.command('zload disk:Tests/RecoveryKeep.ZC', 'zc: loaded disk:Tests/RecoveryKeep.ZC')
                    guest.command('zcall RecoveryKeep', 'zc => 0x00000000000003d7')
            finally:
                guest.close()
        print(f'PASS {label}: installer recovery and user file after cold boot', flush=True)
        disk.unlink()
        var.unlink()
