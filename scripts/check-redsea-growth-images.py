#!/usr/bin/env python3
"""Host-only interrupted-growth prototype checks on disposable file clones."""
import argparse
import pathlib
import struct
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('upgraded', type=pathlib.Path)
parser.add_argument('--binary', type=pathlib.Path, required=True)
args = parser.parse_args()

def entry(attr, name, first, size):
    record = bytearray(64)
    struct.pack_into('<H', record, 0, attr)
    record[2:2 + len(name)] = name
    struct.pack_into('<QQ', record, 40, first, size)
    return record

def legacy_fixture(seed):
    base = 130911
    root, home, file = base + 2, base + 3, base + 4
    boot = bytearray(512); boot[3] = 0x88; boot[510:] = b'\x55\xaa'
    struct.pack_into('<QQQQ', boot, 8, base, 128, root, 1)
    bitmap = bytearray(512); bitmap[0] = 15
    rootdir = bytearray(512)
    rootdir[:64] = entry(0x410, b'.', root, 512)
    rootdir[64:128] = entry(0x410, b'..', root, 0)
    rootdir[128:192] = entry(0x410, b'Home', home, 512)
    homedir = bytearray(512)
    homedir[:64] = entry(0x410, b'.', home, 512)
    homedir[64:128] = entry(0x410, b'..', root, 0)
    homedir[128:192] = entry(0x400, b'Hello.TXT', file, 15)
    data = b'persist-128-test'.ljust(512, b'\0')
    with seed.open('r+b') as stream:
        for lba, data_block in ((base, boot), (base + 1, bitmap),
                                (root, rootdir), (home, homedir), (file, data)):
            stream.seek(lba * 512)
            stream.write(data_block)

def clone(source, destination):
    subprocess.run(['/bin/cp', '-c', str(source), str(destination)], check=True)

cases = [(0, 0)] + [(cut, tear) for cut in
        (4, 8, 1301, 1310, 1320, 1330, 1350, 1390, 1450, 1460, 1465,
         1470, 1500, 2000, 3000) for tear in (7, 256)]
with tempfile.TemporaryDirectory(prefix='redsea-growth-images-') as temporary:
    directory = pathlib.Path(temporary)
    for label, source in (('128m', args.legacy.resolve()), ('1g', args.upgraded.resolve())):
        if not source.is_file():
            raise SystemExit(f'Not a regular image file: {source}')
        seed = directory / 'seed.img'
        clone(source, seed)
        if label == '128m':
            legacy_fixture(seed)
        for cut, tear in cases:
            target = directory / 'disposable.img'
            clone(seed, target)
            result = subprocess.run([str(args.binary.resolve()), str(target), str(cut), str(tear)],
                                    capture_output=True, text=True)
            target.unlink()
            if result.returncode:
                raise SystemExit(f'{label}: cut={cut}, tear={tear}, exit={result.returncode}\n'
                                 f'{result.stdout}{result.stderr}')
        seed.unlink()
        print(f'PASS {label}: {len(cases)} file-backed prototype runs; file and EFI commit preserved', flush=True)
