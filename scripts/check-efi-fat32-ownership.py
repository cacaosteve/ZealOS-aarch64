#!/usr/bin/env python3
"""Exercise FAT ownership checks on disposable copies, including crosslinks."""
import argparse
import pathlib
import shutil
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='efi-ownership-') as name:
    tmp = pathlib.Path(name)
    exe, disk = tmp / 'test', tmp / 'disk.img'
    subprocess.run(['cc', '-O2', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_ownership_test.c'),
                    '-o', str(exe)], check=True)
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        for scenario in ('valid', 'crosslink', 'cycle'):
            shutil.copyfile(source, disk)
            if scenario != 'valid':
                with disk.open('r+b') as f:
                    f.seek(2048 * 512)
                    boot = f.read(512)
                    spc, reserved, fatsz, root = boot[13], struct.unpack_from('<H', boot, 14)[0], struct.unpack_from('<I', boot, 36)[0], struct.unpack_from('<I', boot, 44)[0]
                    fat = 2048 + reserved
                    data = fat + 2 * fatsz
                    if scenario == 'cycle':
                        for copy in range(2):
                            f.seek((fat + copy * fatsz) * 512 + root * 4)
                            f.write(struct.pack('<I', root))
                    else:
                        start = (data + (root - 2) * spc) * 512
                        f.seek(start)
                        entries = bytearray(f.read(spc * 512))
                        original = next(i for i in range(0, len(entries), 32)
                                        if entries[i:i+11] == b'LIMINE~1CON')
                        empty = next(i for i in range(0, len(entries), 32) if entries[i] == 0)
                        entry = bytearray(entries[original:original+32])
                        entry[:11] = b'CROSSLNKTXT'
                        f.seek(start + empty)
                        f.write(entry)
            subprocess.run([str(exe), str(disk), '1' if scenario == 'valid' else '0'], check=True)
            print(f'PASS {label}: {scenario}', flush=True)
