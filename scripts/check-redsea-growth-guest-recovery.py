#!/usr/bin/env python3
"""Test recovery of host-injected directory interruptions in disposable guests."""
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
check = importlib.util.module_from_spec(spec); spec.loader.exec_module(check)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('disk', type=pathlib.Path); p.add_argument('iso', type=pathlib.Path)
p.add_argument('kernel', type=pathlib.Path); p.add_argument('injector', type=pathlib.Path)
args = p.parse_args()
cases = [('installed', 1320, 7), ('installed', 1390, 256), ('installed', 1450, 7),
         ('installed', 1460, 256), ('installer', 1390, 7), ('installer', 1460, 256)]
with tempfile.TemporaryDirectory(prefix='growth-guest-recovery-') as name:
    temp = pathlib.Path(name); disk = temp/'disk.img'; var = temp/'vars.fd'
    for mode, cut, tear in cases:
        subprocess.run(['/bin/cp', '-c', str(args.disk.resolve()), str(disk)], check=True)
        if mode == 'installed':
            subprocess.run(['mcopy', '-o', '-i', str(disk)+'@@1048576',
                            str(args.kernel.resolve()), '::/ZVKERNEL.ELF'], check=True)
        subprocess.run([str(args.injector.resolve()), str(disk), str(cut), str(tear), 'pending'], check=True)
        shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
        log = ROOT / f'build/utm-parity-goal/guest-growth-recovery-{mode}-{cut}.log'
        guest = check.Guest(check.build_command(temp, var, disk, args.iso.resolve() if mode=='installer' else None), log)
        try:
            boot = check.boot_to_prompt(guest, f'{mode} cut={cut}')
            if mode == 'installed':
                if b'RedSea: directory recovery completed' not in boot:
                    raise RuntimeError('installed boot did not report directory recovery')
            else:
                result = guest.command('zinstall recovery', 'zinstall: no pending recovery', timeout=300)
                if b'RedSea: directory recovery completed' not in result:
                    raise RuntimeError('installer did not report directory recovery')
            if mode == 'installed':
                guest.command('zc I64 n=-1; U8 *p=FileRead("disk:/Home/Hello.TXT",&n); '
                              'Print("growth_recovery_file=%d\\n",p!=NULL && n==15 && p[0]==97 && p[14]==5); Free(p);',
                              'growth_recovery_file=1', timeout=180)
        finally:
            guest.close()
        if mode == 'installer':
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            guest = check.Guest(check.build_command(temp, var, disk, None), log.with_name(log.stem+'-disk.log'))
            try:
                check.boot_to_prompt(guest, f'installer cut={cut} disk boot')
                guest.command('zc I64 n=-1; U8 *p=FileRead("disk:/Home/Hello.TXT",&n); '
                              'Print("growth_recovery_file=%d\\n",p!=NULL && n==15 && p[0]==97 && p[14]==5); Free(p);',
                              'growth_recovery_file=1', timeout=180)
            finally:
                guest.close()
        disk.unlink()
        print(f'PASS {mode}: cut={cut}, torn={tear}, recovered and Hello.TXT preserved', flush=True)
