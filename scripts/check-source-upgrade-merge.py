#!/usr/bin/env python3
"""Test the host-only baseline-aware source merge on disposable images."""
import argparse
import hashlib
import pathlib
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / 'scripts/source-volume.py'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
parser.add_argument('bundle', type=pathlib.Path)
args = parser.parse_args()


def digest_range(path, start, stop=None):
    h = hashlib.sha256()
    with path.open('rb') as f:
        f.seek(start)
        left = None if stop is None else stop - start
        while left is None or left > 0:
            b = f.read(1048576 if left is None else min(left, 1048576))
            if not b:
                break
            h.update(b)
            if left is not None:
                left -= len(b)
    return h.digest()


def bank_records(path):
    sectors = path.stat().st_size // 512
    starts = (131039, 196575) if sectors == 262144 else (sectors - 33 - 131072, sectors - 33 - 65536)
    with path.open('rb') as f:
        valid = []
        for index, start in enumerate(starts):
            f.seek(start * 512)
            h = f.read(512)
            if h[:8] == b'ZCSRC001' and struct.unpack_from('<II', h, 8) == (1, 65536):
                valid.append((0, -index, start))
            elif (h[:8] == b'ZCSRC002' and struct.unpack_from('<II', h, 8) == (2, 65536)
                  and struct.unpack_from('<I', h, 16)[0] and
                  struct.unpack_from('<I', h, 20)[0] == zlib.crc32(h[:20])):
                valid.append((struct.unpack_from('<I', h, 16)[0], -index, start))
        if not valid:
            raise RuntimeError('no valid bank')
        base = max(valid)[2]
        result, pos = {}, 1
        while pos < 65536:
            f.seek((base + pos) * 512)
            h = f.read(512)
            if not any(h):
                break
            if h[:8] not in (b'ZCFILE1\0', b'ZCDEL1\0\0'):
                raise RuntimeError('invalid source record')
            length, crc, blocks, n = struct.unpack_from('<IIII', h, 8)
            name = h[32:32+n].decode('ascii')
            data = f.read(length)
            if h[:8] == b'ZCFILE1\0' and (len(data) != length or zlib.crc32(data) != crc):
                raise RuntimeError('bad source data')
            result[name] = None if h[:8] == b'ZCDEL1\0\0' else data
            pos += 1 + blocks
    return result, starts


with tempfile.TemporaryDirectory(prefix='source-baseline-merge-') as folder:
    temp = pathlib.Path(folder)
    disk, base, edit, user = (temp / n for n in ('disk.img', 'old.tar', 'edit.ZC', 'user.ZC'))
    edited_name = 'Demo/Graphics/Lines.ZC'
    deleted_name = 'Demo/Graphics/Clock.ZC'
    user_name = 'Tests/MergeKeep.ZC'
    edit.write_bytes(b'I64 EditedDemo(){return 619;}\n')
    user.write_bytes(b'I64 MergeKeep(){return 823;}\n')
    with tarfile.open(args.bundle) as archive:
        incoming = {item.name: archive.extractfile(item).read() for item in archive
                    if item.isfile() and item.name.endswith(('.ZC', '.HH'))}
    new_system = incoming['System/GuiTerminal.ZC']
    for original in (args.legacy, args.expanded):
        shutil.copyfile(original, disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576', '::/boot/sources.tar', str(base)], check=True)
        with tarfile.open(base) as archive:
            baseline = {item.name: archive.extractfile(item).read() for item in archive
                        if item.isfile() and item.name.endswith(('.ZC', '.HH'))}
        for path, source in ((edited_name, edit), (user_name, user)):
            subprocess.run([sys.executable, str(TOOL), 'put', str(disk), str(source), path], check=True)
        subprocess.run([sys.executable, str(TOOL), 'delete', str(disk), deleted_name], check=True)
        before, starts = bank_records(disk)
        prefix = digest_range(disk, 0, starts[0] * 512)
        suffix = digest_range(disk, (starts[1] + 65536) * 512)
        subprocess.run([sys.executable, str(TOOL), 'merge-baseline', str(disk), str(base),
                        str(args.bundle), '--test-stop-before-publish'], check=True)
        stopped, _ = bank_records(disk)
        if stopped != before:
            raise RuntimeError('unpublished merge changed the selected source bank')
        subprocess.run([sys.executable, str(TOOL), 'merge-baseline', str(disk), str(base),
                        str(args.bundle)], check=True)
        after, _ = bank_records(disk)
        if after.get('System/GuiTerminal.ZC') != new_system:
            raise RuntimeError('unchanged system file was not refreshed')
        if after.get(edited_name) != edit.read_bytes() or after.get(user_name) != user.read_bytes():
            raise RuntimeError('edited or user-created file was not preserved')
        if deleted_name not in after or after[deleted_name] is not None:
            raise RuntimeError('deletion mask was not preserved')
        for path, data in before.items():
            if data is None or path not in baseline or data != baseline[path]:
                if path not in after or after[path] != data:
                    raise RuntimeError('user edit or deletion changed: ' + path)
            elif path in incoming and after.get(path) != incoming[path]:
                raise RuntimeError('unchanged bundled source was not refreshed: ' + path)
        if prefix != digest_range(disk, 0, starts[0] * 512) or \
                suffix != digest_range(disk, (starts[1] + 65536) * 512):
            raise RuntimeError('bytes outside source banks changed')
        print(f'PASS {original}: refreshed system source, edits and deletion preserved', flush=True)
        disk.unlink()
        base.unlink()
