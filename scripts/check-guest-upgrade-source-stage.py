#!/usr/bin/env python3
"""Test full compressed source staging and rollback on disposable guests."""
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
parser.add_argument('installer_iso', type=pathlib.Path)
parser.add_argument('payload', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('--kernel', type=pathlib.Path)
parser.add_argument('--booter', type=pathlib.Path)
args = parser.parse_args()
if bool(args.kernel) != bool(args.booter):
    parser.error('--kernel and --booter must be given together')
with tempfile.TemporaryDirectory(prefix='guest-source-stage-') as name:
    temp = pathlib.Path(name)
    disk, var, extract, esp = (temp / n for n in ('disk.img', 'vars.fd', 'source.lz4', 'esp.img'))
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        for scenario in ('commit', 'interrupt'):
            shutil.copyfile(source, disk)
            phases = ['setup', 'stage'] + (['recover'] if scenario == 'interrupt' else []) + ['verify']
            for phase in phases:
                shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
                iso = (args.diagnostic_iso.resolve() if phase == 'stage' else
                       args.installer_iso.resolve() if phase == 'recover' else None)
                log = ROOT / f'build/utm-parity-goal/source-stage-{label}-{scenario}-{phase}.log'
                log.parent.mkdir(parents=True, exist_ok=True)
                guest = check.Guest(check.build_command(temp, var, disk, iso), log)
                try:
                    check.boot_to_prompt(guest, f'{label} {scenario} {phase}')
                    if phase == 'setup':
                        guest.command('zput Tests/StageKeep.ZC I64 StageKeep(){return 577;}',
                                      'zput: saved Tests/StageKeep.ZC')
                    elif phase == 'stage':
                        cmd = (('stage-all' if scenario == 'commit' else 'interrupt-all') if args.kernel else
                               ('stage-source' if scenario == 'commit' else 'interrupt-source'))
                        expected = 'diagnostic committed' if scenario == 'commit' else 'diagnostic interruption staged'
                        guest.command('zinstall journal-test ' + cmd, expected, timeout=900)
                    elif phase == 'recover':
                        guest.command('zinstall recovery', 'pending recovery completed', timeout=300)
                    else:
                        guest.command('zload disk:Tests/StageKeep.ZC', 'zc: loaded disk:Tests/StageKeep.ZC')
                        guest.command('zcall StageKeep', 'zc => 0x0000000000000241')
                finally:
                    guest.close()
            files = [('ZUSOURCE.LZ4', args.payload)]
            if args.kernel:
                files += [('ZUKERNEL.ELF', args.kernel), ('ZUBOOTER.ELF', args.booter)]
            for filename, expected_path in files:
                copy = subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                                       '::/' + filename, str(extract)], capture_output=True)
                if scenario == 'commit':
                    if copy.returncode or check.file_digest(extract) != check.file_digest(expected_path):
                        raise RuntimeError('staged file differs: ' + filename)
                    extract.unlink()
                elif copy.returncode == 0:
                    raise RuntimeError('interrupted file still visible after rollback: ' + filename)
            with disk.open('rb') as src, esp.open('wb') as dst:
                src.seek(1048576)
                dst.write(src.read(128863 * 512))
            subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
            print(f'PASS {label}: {scenario}, user file and EFI checks', flush=True)
            for path in (disk, var, esp):
                path.unlink()
