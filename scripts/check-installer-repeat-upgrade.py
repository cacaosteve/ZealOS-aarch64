#!/usr/bin/env python3
"""Test consecutive graphical upgrades and preserved changes on disposable disks."""
import argparse
import importlib.util
import pathlib
import shutil
import sys
import tempfile
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('installer_check', ROOT / 'scripts/check-iso-installer.py')
check = importlib.util.module_from_spec(spec); spec.loader.exec_module(check)
from installer_input import stage, text, qmp
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('--passes', type=int, default=3)
parser.add_argument('--capture-progress', action='store_true', help='capture the upgrade screen while work is running')
parser.add_argument('--fault-iso', type=pathlib.Path)
parser.add_argument('--fault-type', choices=('reclaim', 'switch', 'marker', 'manifest'), default='reclaim')
parser.add_argument('--next-iso', type=pathlib.Path)
parser.add_argument('--collision', action='store_true', help='check an unknown inactive EFI file is preserved')
parser.add_argument('--tamper-slot', action='store_true', help='check modified inactive EFI files are preserved')
parser.add_argument('--log-id', default='repeat', help='prefix for per-guest logs')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='repeat-upgrade-') as name:
    tmp = pathlib.Path(name); disk, var = tmp / 'disk.img', tmp / 'vars.fd'
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        phases = ['setup']
        for n in range(args.passes):
            if n and args.fault_iso: phases.append(f'fault-{n}')
            phases.extend((f'upgrade-{n}', f'verify-{n}'))
        for phase in phases:
            if args.collision and phase == 'upgrade-1':
                collision = tmp / 'collision.bin'
                collision.write_bytes(b'user EFI file that must survive\n')
                subprocess.run(['mcopy', '-i', str(disk) + '@@1048576', str(collision), '::/ZVSOURCE.LZ4'], check=True)
            if args.tamper_slot and phase == 'upgrade-2':
                collision = tmp / 'tampered.bin'
                collision.write_bytes(b'modified inactive EFI file must survive\n')
                subprocess.run(['mcopy', '-o', '-i', str(disk) + '@@1048576', str(collision), '::/ZUBOOTER.ELF'], check=True)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            next_upgrade = phase.startswith('upgrade') and int(phase.split('-')[1]) > 0 and args.next_iso
            iso = (args.next_iso.resolve() if next_upgrade else
                   args.iso.resolve() if phase.startswith('upgrade') else
                   args.fault_iso.resolve() if phase.startswith('fault') else None)
            log = ROOT / f'build/utm-parity-goal/{args.log_id}-{label}-{phase}.log'
            guest = check.Guest(check.build_command(tmp, var, disk, iso), log)
            try:
                boot = check.boot_to_prompt(guest, f'{label} {phase}')
                if phase == 'setup':
                    guest.command('zput Tests/RepeatKeep.ZC I64 RepeatKeep(){return 811;}', 'zput: saved Tests/RepeatKeep.ZC')
                    guest.command('zput Demo/Graphics/Lines.ZC I64 RepeatEdit(){return 617;}', 'zput: saved Demo/Graphics/Lines.ZC')
                    guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
                elif phase.startswith('fault'):
                    command = {'switch': ('repeat-switch-fault', 'repeat switch fault injected'),
                               'marker': ('repeat-marker-fault', 'repeat marker fault injected'),
                               'manifest': ('repeat-manifest-fault', 'repeat manifest fault injected'),
                               'reclaim': ('repeat-fault', 'repeat fault injected')}[args.fault_type]
                    result = guest.command('zinstall journal-test ' + command[0], command[1], timeout=1800)
                    if (b'upgrade failed' if args.fault_type in ('switch', 'manifest') else b'upgrade preparation failed') not in result:
                        raise RuntimeError('repeat upgrade did not stop after the injected fault')
                elif phase.startswith('upgrade'):
                    guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}', 'zc: executed')
                    stage(guest, 5); text(guest.qmp_path, 'y')
                    stage(guest, 7, timeout=900); text(guest.qmp_path, 'y')
                    if args.capture_progress:
                        time.sleep(0.5)
                        capture = ROOT / f'build/utm-parity-goal/{args.log_id}-{label}-{phase}-progress.ppm'
                        qmp(guest.qmp_path, 'screendump', {'filename': str(capture)})
                        print(f'Captured upgrade screen: {capture}', flush=True)
                    if (args.collision and phase == 'upgrade-1') or (args.tamper_slot and phase == 'upgrade-2'):
                        stage(guest, -2, timeout=1800)
                    else:
                        stage(guest, 6, timeout=1800); text(guest.qmp_path, 'n')
                else:
                    n = int(phase.split('-')[1]); blocked = (args.collision and n == 1) or (args.tamper_slot and n == 2)
                    slot = b'/ZVKERNEL.ELF' if args.tamper_slot and n == 2 else (b'/ZUKERNEL.ELF' if n % 2 == 0 or blocked else b'/ZVKERNEL.ELF')
                    if b'module: ' + slot not in boot or (not blocked and b'committed source bank published' not in boot):
                        raise RuntimeError('unexpected boot slot or missing source publication')
                    guest.command('zload disk:Tests/RepeatKeep.ZC', 'zc: loaded disk:Tests/RepeatKeep.ZC')
                    guest.command('zcall RepeatKeep', 'zc => 0x000000000000032b')
                    guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                    guest.command('zcall RepeatEdit', 'zc => 0x0000000000000269')
                    guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
                    if n == 0 and args.passes > 1:
                        guest.command('zput Tests/Between.ZC I64 Between(){return 719;}',
                                      'zput: saved Tests/Between.ZC')
                    if n > 0:
                        guest.command('zload disk:Tests/Between.ZC', 'zc: loaded disk:Tests/Between.ZC')
                        guest.command('zcall Between', 'zc => 0x00000000000002cf')
                    if n > 0 and args.next_iso:
                        guest.command('zload disk:Tests/RepeatNew.ZC', 'zc: loaded disk:Tests/RepeatNew.ZC')
                        guest.command('zcall RepeatNew', 'zc => 0x00000000000003a3')
            finally:
                guest.close()
        esp = tmp / 'esp.img'
        with disk.open('rb') as src, esp.open('wb') as dst:
            src.seek(2048 * 512); dst.write(src.read(128863 * 512))
        subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
        if args.collision:
            result = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/ZVSOURCE.LZ4'],
                                    stdout=subprocess.PIPE, check=True).stdout
            if result != b'user EFI file that must survive\n': raise RuntimeError('unknown EFI file changed')
        if args.tamper_slot:
            result = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/ZUBOOTER.ELF'],
                                    stdout=subprocess.PIPE, check=True).stdout
            if result != b'modified inactive EFI file must survive\n': raise RuntimeError('modified EFI file changed')
        esp.unlink()
        outcome = 'protected unknown EFI file' if args.collision else ('protected modified EFI file' if args.tamper_slot else f'{args.passes} graphical upgrades')
        print(f'PASS {label}: {outcome}, preserved changes and valid FAT', flush=True)
