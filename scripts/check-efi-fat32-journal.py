#!/usr/bin/env python3
"""Test journaled FAT writes on disposable copies, using host file I/O."""
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
parser.add_argument('--slots', type=int, default=512)
args = parser.parse_args()


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.digest()


with tempfile.TemporaryDirectory(prefix='efi-fat-journal-') as name:
    temp = pathlib.Path(name)
    executable, disk = temp / 'test', temp / 'disk.img'
    before, after, esp = temp / 'before', temp / 'after', temp / 'esp.img'
    subprocess.run(['cc', '-std=c11', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_journal_test.c'),
                    '-o', str(executable)], check=True)
    for original in (args.legacy, args.expanded):
        shutil.copyfile(original, disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                        '::/boot/sources.tar', str(before)], check=True)
        for stop in (1, 3, 6, 0):
            shutil.copyfile(original, disk)
            subprocess.run([str(executable), str(disk), 'stage', str(stop), str(args.slots)], check=True)
            subprocess.run([str(executable), str(disk), 'recover', '0', str(args.slots)], check=True)
            subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                            '::/boot/sources.tar', str(after)], check=True)
            if digest(before) != digest(after):
                raise RuntimeError('existing source archive changed')
            after.unlink()
            if not stop:
                subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                                '::/ZEALTST.BIN', str(after)], check=True)
                if after.read_bytes() != bytes((i * 73 + i // 19) & 255 for i in range(100003)):
                    raise RuntimeError('committed file differs')
                after.unlink()
            with disk.open('rb') as src, esp.open('wb') as dst:
                src.seek(1048576)
                dst.write(src.read(128863 * 512))
            subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
            esp.unlink()
            print(f'PASS {original}: ' + (f'interruption {stop}' if stop else 'commit'), flush=True)
        before.unlink()
        disk.unlink()
