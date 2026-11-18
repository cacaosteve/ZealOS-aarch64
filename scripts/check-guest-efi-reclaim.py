#!/usr/bin/env python3
"""Test guest reclaim after an actual disposable installer upgrade."""
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
from installer_input import stage, text
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('installer_iso', type=pathlib.Path)
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('--recovery-iso', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='guest-reclaim-') as name:
    temp = pathlib.Path(name)
    disk, var, esp = temp / 'disk.img', temp / 'vars.fd', temp / 'esp.img'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        for phase in ('setup', 'unsafe', 'upgrade', 'post-upgrade', 'interrupt', 'recover', 'verify'):
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = (args.recovery_iso.resolve() if phase == 'recover' and args.recovery_iso else
                   args.installer_iso.resolve() if phase == 'upgrade' else
                   args.diagnostic_iso.resolve() if phase in ('unsafe', 'interrupt', 'recover') else None)
            log = ROOT / f'build/utm-parity-goal/guest-reclaim-{label}-{phase}.log'
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                boot = check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/ReclaimKeep.ZC I64 ReclaimKeep(){return 811;}',
                                  'zput: saved Tests/ReclaimKeep.ZC')
                    guest.command('zput Demo/Graphics/Lines.ZC I64 ReclaimEdit(){return 617;}',
                                  'zput: saved Demo/Graphics/Lines.ZC')
                    guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
                elif phase == 'unsafe':
                    guest.command('zinstall journal-test reclaim-start',
                                  'reclaim refused; active boot files not verified', timeout=900)
                elif phase == 'upgrade':
                    guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}', 'zc: executed')
                    stage(guest, 5); text(guest.qmp_path, 'y')
                    stage(guest, 7); text(guest.qmp_path, 'y')
                    stage(guest, 6, timeout=900); text(guest.qmp_path, 'n')
                elif phase == 'post-upgrade':
                    if b'committed source bank published' not in boot:
                        raise RuntimeError('upgrade did not publish the source bank')
                    guest.command('zload disk:Tests/ReclaimKeep.ZC', 'zc: loaded disk:Tests/ReclaimKeep.ZC')
                elif phase == 'interrupt':
                    guest.command('zinstall journal-test reclaim-start', 'reclaim cursor started', timeout=900)
                    guest.command('zinstall journal-test reclaim-interrupt', 'reclaim interruption staged', timeout=900)
                elif phase == 'recover':
                    if args.recovery_iso:
                        guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}', 'zc: executed')
                        stage(guest, 5); text(guest.qmp_path, 'y')
                        stage(guest, 7, timeout=900); text(guest.qmp_path, 'n')
                        stage(guest, 2); text(guest.qmp_path, 'n')
                        stage(guest, -1)
                        guest.command('zinstall recovery', 'no pending recovery', timeout=900)
                    else:
                        guest.command('zinstall journal-test reclaim-recover', 'reclaim journal recovered', timeout=900)
                        for i in range(16):
                            result = guest.command('zinstall journal-test reclaim-step', 'zinstall: reclaim ', timeout=900)
                            if b'reclaim complete' in result:
                                break
                            if b'reclaim batch complete' not in result:
                                raise RuntimeError('reclaim batch failed')
                        else:
                            raise RuntimeError('reclaim did not finish')
                else:
                    if b'module: /ZUKERNEL.ELF' not in boot or b'compressed source archive verified' not in boot:
                        raise RuntimeError('reclaimed disk did not boot its upgraded files')
                    guest.command('zload disk:Tests/ReclaimKeep.ZC', 'zc: loaded disk:Tests/ReclaimKeep.ZC')
                    guest.command('zcall ReclaimKeep', 'zc => 0x000000000000032b')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall ReclaimEdit', 'zc => 0x0000000000000269')
                    guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
            finally:
                guest.close()
        with disk.open('rb') as src, esp.open('wb') as dst:
            src.seek(2048 * 512); dst.write(src.read(128863 * 512))
        subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
        old = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/boot/sources.tar'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if old.returncode == 0:
            raise RuntimeError('old archive was not reclaimed')
        print(f'PASS {label}: guest reclaim, interruption recovery and preserved file', flush=True)
