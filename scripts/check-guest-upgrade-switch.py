#!/usr/bin/env python3
"""Exercise diagnostic boot switching and rollback on disposable installs."""
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
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('--require-current-system-source', action='store_true',
                    help='also require the new installer recovery code in the installed source')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='upgrade-switch-') as name:
    temp = pathlib.Path(name)
    disk, var, esp = temp / 'disk.img', temp / 'vars.fd', temp / 'esp.img'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        for scenario in ('commit', 'interrupt'):
            shutil.copyfile(source, disk)
            phases = ['setup', 'switch'] + (['recover'] if scenario == 'interrupt' else []) + ['verify']
            for phase in phases:
                shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
                iso = (args.diagnostic_iso.resolve() if phase == 'switch' else
                       args.installer_iso.resolve() if phase == 'recover' else None)
                log = ROOT / f'build/utm-parity-goal/switch-{label}-{scenario}-{phase}.log'
                log.parent.mkdir(parents=True, exist_ok=True)
                guest = check.Guest(check.build_command(temp, var, disk, iso), log)
                try:
                    boot = check.boot_to_prompt(guest, f'{label} {scenario} {phase}')
                    if phase == 'setup':
                        guest.command('zput Tests/SwitchKeep.ZC I64 SwitchKeep(){return 379;}',
                                      'zput: saved Tests/SwitchKeep.ZC')
                    elif phase == 'switch':
                        command = 'switch' if scenario == 'commit' else 'interrupt-switch'
                        expected = 'diagnostic committed' if scenario == 'commit' else 'diagnostic interruption staged'
                        guest.command('zinstall journal-test ' + command, expected, timeout=900)
                    elif phase == 'recover':
                        guest.command('zinstall recovery', 'pending recovery completed', timeout=300)
                    else:
                        switched = b'module: /ZUKERNEL.ELF' in boot and b'compressed source archive verified' in boot
                        if switched != (scenario == 'commit'):
                            raise RuntimeError('booted the wrong kernel or archive after ' + scenario)
                        guest.command('zload disk:Tests/SwitchKeep.ZC', 'zc: loaded disk:Tests/SwitchKeep.ZC')
                        guest.command('zcall SwitchKeep', 'zc => 0x000000000000017b')
                        if scenario == 'commit' and args.require_current_system_source:
                            guest.command('zc I64 n; U8 *s=FileRead("/System/GuiTerminal.ZC",&n); '
                                          'Print("upgrade_source_current=%d\\n",s && !!StrMatch("zinstall recovery",s)); Free(s);',
                                          'upgrade_source_current=1')
                finally:
                    guest.close()
            with disk.open('rb') as src, esp.open('wb') as dst:
                src.seek(1048576)
                dst.write(src.read(128863 * 512))
            subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
            print(f'PASS {label}: diagnostic {scenario}, boot and existing user file', flush=True)
            for path in (disk, var, esp):
                path.unlink()
