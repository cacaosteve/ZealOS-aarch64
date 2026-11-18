#!/usr/bin/env python3
"""Exercise the narrow EFI writer on disposable copies of two installs."""
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


def checksum(path, start, end=None):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        stream.seek(start)
        left = None if end is None else end - start
        while left is None or left > 0:
            part = stream.read(1048576 if left is None else min(left, 1048576))
            if not part:
                break
            result.update(part)
            if left is not None:
                left -= len(part)
    return result.digest()


with tempfile.TemporaryDirectory(prefix='efi-writer-') as folder:
    tmp = pathlib.Path(folder)
    executable = tmp / 'test'
    subprocess.run(['cc', '-std=c11', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_write_test.c'),
                    '-o', str(executable)], check=True)
    for original in (args.legacy, args.expanded):
        disk, extracted, esp = tmp / 'disk.img', tmp / 'file', tmp / 'esp.img'
        shutil.copyfile(original, disk)
        original_files = {}
        for name in ('kernel.elf', 'zealbooter.elf', 'sources.tar'):
            subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                            '::/boot/' + name, str(extracted)], check=True)
            original_files[name] = checksum(extracted, 0)
            extracted.unlink()
        subprocess.run([str(executable), str(disk)], check=True)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                        '::/ZEALTST.BIN', str(extracted)], check=True)
        if extracted.read_bytes() != bytes((i * 73 + i // 19) & 255 for i in range(16304626)):
            raise RuntimeError('independent file read-back differs')
        if (checksum(original, 0, 1048576) != checksum(disk, 0, 1048576) or
                checksum(original, 130911 * 512) != checksum(disk, 130911 * 512)):
            raise RuntimeError('data outside EFI partition changed')
        extracted.unlink()
        for name in original_files:
            subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                            '::/boot/' + name, str(extracted)], check=True)
            if checksum(extracted, 0) != original_files[name]:
                raise RuntimeError('existing EFI file changed: ' + name)
            extracted.unlink()
        with disk.open('rb') as src, esp.open('wb') as dst:
            src.seek(1048576)
            dst.write(src.read(128863 * 512))
        subprocess.run(['fsck.fat', '-n', str(esp)], check=True)
        print(f'PASS {original}: file verified; non-EFI bytes unchanged', flush=True)
        for path in (disk, esp):
            path.unlink()
