#!/usr/bin/env python3
"""Exercise host-side source merge plus diagnostic switch on disposable guests.

This is not a production upgrade test: the host modifies the source bank
separately from the guest's EFI transaction.
"""
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
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('bundle', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='upgrade-merge-prototype-') as name:
    temp = pathlib.Path(name)
    disk, var, baseline = temp / 'disk.img', temp / 'vars.fd', temp / 'baseline.tar'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                        '::/boot/sources.tar', str(baseline)], check=True)
        for phase in ('setup', 'switch', 'verify'):
            if phase == 'switch':
                subprocess.run([sys.executable, str(ROOT / 'scripts/source-volume.py'),
                                'merge-baseline', str(disk), str(baseline), str(args.bundle)], check=True)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = args.diagnostic_iso.resolve() if phase == 'switch' else None
            log = ROOT / f'build/utm-parity-goal/merge-prototype-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                boot = check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/MergeKeep.ZC I64 MergeKeep(){return 823;}',
                                  'zput: saved Tests/MergeKeep.ZC')
                    guest.command('zput Demo/Graphics/Lines.ZC I64 EditedDemo(){return 619;}',
                                  'zput: saved Demo/Graphics/Lines.ZC')
                elif phase == 'switch':
                    guest.command('zinstall journal-test switch', 'diagnostic committed', timeout=900)
                else:
                    if b'module: /ZUKERNEL.ELF' not in boot or b'compressed source archive verified' not in boot:
                        raise RuntimeError('did not boot diagnostic upgrade files')
                    guest.command('zc I64 n; U8 *s=FileRead("/System/GuiTerminal.ZC",&n); '
                                  'Print("upgrade_source_current=%d\\n",s && !!StrMatch("zinstall recovery",s)); Free(s);',
                                  'upgrade_source_current=1')
                    guest.command('zload disk:Tests/MergeKeep.ZC', 'zc: loaded disk:Tests/MergeKeep.ZC')
                    guest.command('zcall MergeKeep', 'zc => 0x0000000000000337')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall EditedDemo', 'zc => 0x000000000000026b')
            finally:
                guest.close()
        print(f'PASS {label}: host source merge and diagnostic boot switch', flush=True)
        for path in (disk, var, baseline):
            path.unlink()
