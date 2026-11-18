#!/usr/bin/env python3
"""Test native unpublished source staging on disposable disk images."""
import argparse
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / 'scripts/source-volume.py'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('bundle', type=pathlib.Path)
args = parser.parse_args()


def digest(path, start, stop=None):
    h = hashlib.sha256()
    with path.open('rb') as f:
        f.seek(start)
        left = None if stop is None else stop - start
        while left is None or left:
            b = f.read(1048576 if left is None else min(left, 1048576))
            if not b:
                break
            h.update(b)
            if left is not None:
                left -= len(b)
    return h.digest()


def records(path, base):
    result, pos = {}, 1
    with path.open('rb') as f:
        while pos < 65536:
            f.seek((base + pos) * 512)
            header = f.read(512)
            if not any(header):
                break
            if header[:8] not in (b'ZCFILE1\0', b'ZCDEL1\0\0'):
                raise RuntimeError('invalid candidate record')
            length, crc, blocks, n = struct.unpack_from('<IIII', header, 8)
            name = header[32:32+n].decode('ascii')
            data = f.read(length)
            if header[:8] == b'ZCFILE1\0' and (len(data) != length or zlib.crc32(data) != crc):
                raise RuntimeError('candidate data mismatch: ' + name)
            result[name] = None if header[:8] == b'ZCDEL1\0\0' else data
            pos += 1 + blocks
    return result


with tempfile.TemporaryDirectory(prefix='source-upgrade-stage-') as name:
    temp = pathlib.Path(name)
    disk, reference, baseline, executable = (temp / n for n in ('disk.img', 'reference.img', 'old.tar', 'test'))
    edit, user = temp / 'edit.ZC', temp / 'user.ZC'
    edit.write_bytes(b'I64 EditedDemo(){return 619;}\n')
    user.write_bytes(b'I64 MergeKeep(){return 823;}\n')
    subprocess.run(['cc', '-O2', '-std=c11', '-D_FILE_OFFSET_BITS=64', '-D_POSIX_C_SOURCE=200809L',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    str(ROOT / 'tests/source_upgrade_stage_test.c'), '-o', str(executable)], check=True)
    for original in (args.legacy, args.expanded):
        shutil.copyfile(original, disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576', '::/boot/sources.tar', str(baseline)], check=True)
        for path, file in (('Demo/Graphics/Lines.ZC', edit), ('Tests/MergeKeep.ZC', user)):
            subprocess.run([sys.executable, str(TOOL), 'put', str(disk), str(file), path], check=True)
        subprocess.run([sys.executable, str(TOOL), 'delete', str(disk), 'Demo/Graphics/Clock.ZC'], check=True)
        shutil.copyfile(disk, reference)
        sectors = disk.stat().st_size // 512
        a, b = (131039, 196575) if sectors == 262144 else (sectors - 33 - 131072, sectors - 33 - 65536)
        active_before = digest(disk, a * 512, (a + 65536) * 512)
        outside_before = (digest(disk, 0, b * 512), digest(disk, (b + 65536) * 512))
        for stop in (1, 25):
            subprocess.run([str(executable), str(disk), str(args.bundle), str(stop)], check=True)
            if digest(disk, a * 512, (a + 65536) * 512) != active_before:
                raise RuntimeError('interrupted staging modified the active bank')
        subprocess.run([str(executable), str(disk), str(args.bundle)], check=True)
        with disk.open('rb') as f:
            f.seek(b * 512)
            if any(f.read(512)):
                raise RuntimeError('inactive candidate was published prematurely')
        subprocess.run([sys.executable, str(TOOL), 'merge-baseline', str(reference),
                        str(baseline), str(args.bundle)], check=True)
        expected = records(reference, b)
        if records(disk, b) != expected:
            raise RuntimeError('native candidate differs from reference merge')
        if outside_before != (digest(disk, 0, b * 512), digest(disk, (b + 65536) * 512)):
            raise RuntimeError('staging modified bytes outside inactive bank')
        subprocess.run([str(executable), str(disk), str(args.bundle), 'publish'], check=True)
        with disk.open('rb') as f:
            f.seek(b * 512)
            if f.read(8) != b'ZCSRC002':
                raise RuntimeError('verified candidate was not published')
        if records(disk, b) != expected:
            raise RuntimeError('published bank differs from reference merge')
        if outside_before != (digest(disk, 0, b * 512), digest(disk, (b + 65536) * 512)):
            raise RuntimeError('publication modified bytes outside inactive bank')
        print(f'PASS {original}: candidate and publication match reference; original bank unchanged', flush=True)
        for path in (disk, reference, baseline):
            path.unlink()
