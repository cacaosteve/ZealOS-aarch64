#!/usr/bin/env python3
"""Exercise interrupted diagnostic EFI switch and ISO rollback."""
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
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='guest-source-pending-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        for phase in ('setup', 'diagnostic', 'recover', 'verify', 'retry', 'verify-retry'):
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = args.diagnostic_iso.resolve() if phase in ('diagnostic', 'recover', 'retry') else None
            log = ROOT / f'build/utm-parity-goal/coordinated-interrupt-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                boot = check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/PendingKeep.ZC I64 PendingKeep(){return 811;}',
                                  'zput: saved Tests/PendingKeep.ZC')
                    guest.command('zput Demo/Graphics/Lines.ZC I64 PendingEdit(){return 617;}',
                                  'zput: saved Demo/Graphics/Lines.ZC')
                    guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
                elif phase == 'diagnostic':
                    guest.command('zinstall journal-test prepare-bank', 'diagnostic source bank prepared', timeout=900)
                    guest.command('zinstall journal-test interrupt-pending', 'diagnostic interruption staged', timeout=900)
                elif phase == 'recover':
                    guest.command('zinstall recovery', 'pending recovery completed', timeout=300)
                    guest.command('zinstall journal-test cancel-bank', 'diagnostic source cancellation complete', timeout=900)
                elif phase == 'retry':
                    guest.command('zinstall journal-test prepare-bank', 'diagnostic source bank prepared', timeout=900)
                    guest.command('zinstall journal-test switch-pending', 'diagnostic committed', timeout=900)
                else:
                    if phase == 'verify' and b'module: /ZUKERNEL.ELF' in boot:
                        raise RuntimeError('rollback did not restore original boot path')
                    if phase == 'verify-retry' and (b'module: /ZUKERNEL.ELF' not in boot or
                            b'upgrade diagnostic: committed source bank published' not in boot):
                        raise RuntimeError('retry did not complete the upgrade')
                    guest.command('zload disk:Tests/PendingKeep.ZC', 'zc: loaded disk:Tests/PendingKeep.ZC')
                    guest.command('zcall PendingKeep', 'zc => 0x000000000000032b')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall PendingEdit', 'zc => 0x0000000000000269')
                    guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
            finally:
                guest.close()
        print(f'PASS {label}: rollback, cleanup, retry and preserved user files', flush=True)
        disk.unlink()
        var.unlink()
