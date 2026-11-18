#!/usr/bin/env python3
"""Test reclaiming only inactive EFI slot files on disposable copies."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='efi-slot-reclaim-') as name:
    tmp = pathlib.Path(name)
    exe, disk, prepared, config, payload = (tmp / n for n in ('test', 'disk.img', 'prepared.img', 'limine.conf', 'payload'))
    subprocess.run(['cc', '-O2', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_reclaim_test.c'), '-o', str(exe)], check=True)
    def run(action, fail=0):
        return subprocess.run([str(exe), str(disk), action, str(fail)], check=True, capture_output=True, text=True).stdout
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, prepared)
        for src, dst in (('build/upgrade-sources.lz4', '::/ZUSOURCE.LZ4'), ('build/kernel.elf', '::/ZUKERNEL.ELF'),
                         ('build/zealbooter.elf', '::/ZUBOOTER.ELF')):
            subprocess.run(['mcopy', '-i', str(prepared) + '@@1048576', str(ROOT / src), dst], check=True)
        old = subprocess.run(['mtype', '-i', str(prepared) + '@@1048576', '::/limine.conf'], stdout=subprocess.PIPE, check=True).stdout
        current = ('timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n'
                   '/ZealOS aarch64 zealbooter\n    protocol: limine\n'
                   '    path: boot():/ZUBOOTER.ELF\n    module_path: boot():/ZUKERNEL.ELF\n'
                   '    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/ZUSOURCE.LZ4\n'
                   '    kaslr: no\n    resolution: 800x600x32\n').encode()
        config.write_bytes(current.ljust(len(old), b'\n'))
        subprocess.run(['mcopy', '-o', '-i', str(prepared) + '@@1048576', str(config), '::/limine.conf'], check=True)
        for n in ('ZVSOURCE.LZ4', 'ZVKERNEL.ELF', 'ZVBOOTER.ELF'):
            payload.write_bytes((n.encode() * 4096)[:32768])
            subprocess.run(['mcopy', '-i', str(prepared) + '@@1048576', str(payload), '::/' + n], check=True)
        expected = hashlib.sha256((ROOT / 'build/upgrade-sources.lz4').read_bytes()).digest()
        for failure in (0, 1, 1303, 1306, 1310, 1315):
            shutil.copyfile(prepared, disk)
            for i, n in enumerate(('ZVSOURCE.LZ4', 'ZVKERNEL.ELF', 'ZVBOOTER.ELF')):
                run('root' + str(i))
                if failure:
                    if 'INTERRUPTED' not in run('step', failure):
                        raise RuntimeError(f'failure {failure} was not reached')
                    run('recover')
                for attempt in range(8):
                    if 'done=1' in run('step'):
                        break
                else:
                    raise RuntimeError('inactive reclaim did not finish')
                if subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/' + n], stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode == 0:
                    raise RuntimeError('inactive file was not reclaimed')
            active = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/ZUSOURCE.LZ4'],
                                    stdout=subprocess.PIPE, check=True).stdout
            if hashlib.sha256(active).digest() != expected:
                raise RuntimeError('active compressed archive changed')
            print(f'PASS {label}: inactive slot cleanup, interrupted write {failure}', flush=True)
