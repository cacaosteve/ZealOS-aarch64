#!/usr/bin/env python3
"""Check diagnostic source-baseline comparison on disposable installs."""
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
with tempfile.TemporaryDirectory(prefix='guest-source-compare-') as name:
    temp = pathlib.Path(name)
    disk, var = temp / 'disk.img', temp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        before = None
        for phase in ('setup', 'compare', 'verify'):
            if phase == 'compare':
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
                    expected = 'source baseline unchanged 886 edited 1 deleted 1 missing 161 user-only '
                    result = guest.command('zinstall journal-test baseline-compare', expected, timeout=900)
                    user_only = int(result.split(expected.encode(), 1)[1].split()[0])
                    if user_only < 1:
                        raise RuntimeError('guest-created source was not classified as user-only')
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
