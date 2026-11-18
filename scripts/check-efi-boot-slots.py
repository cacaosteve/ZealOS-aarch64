#!/usr/bin/env python3
"""Check exact EFI boot-slot recognition on disposable disk copies."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='efi-boot-slots-') as name:
    tmp = pathlib.Path(name)
    exe, disk, config = tmp / 'test', tmp / 'disk.img', tmp / 'limine.conf'
    subprocess.run(['cc', '-O2', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', str(ROOT / 'tests/efi_boot_slots_test.c'),
                    '-o', str(exe)], check=True)
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        subprocess.run([str(exe), str(disk), '1'], check=True)
        original = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/limine.conf'],
                                  stdout=subprocess.PIPE, check=True).stdout
        for slot, expected in (('U', '2'), ('V', '3'), ('X', '0')):
            current = ('timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n'
                       '/ZealOS aarch64 zealbooter\n    protocol: limine\n'
                       f'    path: boot():/Z{slot}BOOTER.ELF\n    module_path: boot():/Z{slot}KERNEL.ELF\n'
                       f'    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/Z{slot}SOURCE.LZ4\n'
                       '    kaslr: no\n    resolution: 800x600x32\n').encode()
            config.write_bytes(current.ljust(len(original), b'\n'))
            subprocess.run(['mcopy', '-o', '-i', str(disk) + '@@1048576', str(config), '::/limine.conf'], check=True)
            subprocess.run([str(exe), str(disk), expected], check=True)
        print(f'PASS {label}: legacy, both slots and unknown configuration', flush=True)
