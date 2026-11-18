#!/usr/bin/env python3
"""Exercise the normal graphical Upgrade choice on disposable installs."""
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
parser.add_argument('iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('--interrupt-iso', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='installer-upgrade-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        phases = ['setup'] + (['interrupt'] if args.interrupt_iso else []) + ['upgrade', 'verify']
        for phase in phases:
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = (args.iso.resolve() if phase == 'upgrade' else
                   args.interrupt_iso.resolve() if phase == 'interrupt' else None)
            log = ROOT / f'build/utm-parity-goal/upgrade-choice-{label}-{phase}.log'
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                boot = check.boot_to_prompt(guest, label + ' ' + phase)
                if phase == 'setup':
                    guest.command('zput Tests/UpgradeKeep.ZC I64 UpgradeKeep(){return 811;}',
                                  'zput: saved Tests/UpgradeKeep.ZC')
                    guest.command('zput Demo/Graphics/Lines.ZC I64 UpgradeEdit(){return 617;}',
                                  'zput: saved Demo/Graphics/Lines.ZC')
                    guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
                elif phase == 'interrupt':
                    guest.command('zinstall journal-test prepare-bank', 'diagnostic source bank prepared', timeout=900)
                    guest.command('zinstall journal-test interrupt-pending', 'diagnostic interruption staged', timeout=900)
                elif phase == 'upgrade':
                    guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}', 'zc: executed')
                    stage(guest, 5)
                    text(guest.qmp_path, 'y')
                    stage(guest, 7)
                    text(guest.qmp_path, 'y')
                    stage(guest, 6, timeout=900)
                    text(guest.qmp_path, 'n')
                else:
                    if b'module: /ZUKERNEL.ELF' not in boot or b'committed source bank published' not in boot:
                        raise RuntimeError('new boot path or source publication missing')
                    guest.command('zload disk:Tests/UpgradeKeep.ZC', 'zc: loaded disk:Tests/UpgradeKeep.ZC')
                    guest.command('zcall UpgradeKeep', 'zc => 0x000000000000032b')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall UpgradeEdit', 'zc => 0x0000000000000269')
                    guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
            finally:
                guest.close()
        print(f'PASS {label}: graphical Upgrade and preserved files', flush=True)
