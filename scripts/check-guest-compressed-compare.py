#!/usr/bin/env python3
"""Check compressed source-baseline comparison on disposable installs."""
import argparse
import importlib.util
import pathlib
import shutil
import sys
import tempfile
import subprocess
import re

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
with tempfile.TemporaryDirectory(prefix='guest-source-compare-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        before = None
        for phase in ('setup', 'compare', 'verify'):
            if phase == 'compare':
                subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                                str(ROOT / 'build/upgrade-sources.lz4'), '::/ZUSOURCE.LZ4'], check=True)
                before = check.file_digest(disk)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            iso = args.diagnostic_iso.resolve() if phase == 'compare' else None
            log = ROOT / f'build/utm-parity-goal/source-compare-{label}-{phase}.log'
            log.parent.mkdir(parents=True, exist_ok=True)
            guest = check.Guest(check.build_command(temp, var, disk, iso), log)
            try:
                check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Demo/Graphics/Lines.ZC I64 EditedDemo(){return 619;}',
                                  'zput: saved Demo/Graphics/Lines.ZC')
                    guest.command('zput Tests/CompareKeep.ZC I64 CompareKeep(){return 541;}',
                                  'zput: saved Tests/CompareKeep.ZC')
                    guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
                elif phase == 'compare':
                    result = guest.command('zinstall journal-test compressed-compare',
                                           'compressed baseline unchanged ', timeout=900)
                    match = re.search(rb'compressed baseline unchanged (\d+) edited (\d+) deleted (\d+) missing (\d+) user-only (\d+)', result)
                    if not match or int(match[1]) < 1 or int(match[2]) < 1 or int(match[3]) != 1 or int(match[5]) < 1:
                        raise RuntimeError('compressed comparison did not preserve user changes')
                else:
                    guest.command('zload disk:Tests/CompareKeep.ZC', 'zc: loaded disk:Tests/CompareKeep.ZC')
                    guest.command('zcall CompareKeep', 'zc => 0x000000000000021d')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall EditedDemo', 'zc => 0x000000000000026b')
                    guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
            finally:
                guest.close()
            if phase == 'compare' and check.file_digest(disk) != before:
                raise RuntimeError('baseline comparison changed the target')
        print(f'PASS {label}: read-only comparison and user changes survived cold boot', flush=True)
        disk.unlink()
        var.unlink()
