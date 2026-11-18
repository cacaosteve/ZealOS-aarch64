#!/usr/bin/env python3
"""Test diagnostic guest staging of an unpublished merged source bank."""
import argparse
import hashlib
import importlib.util
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('installer_check', ROOT / 'scripts/check-iso-installer.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('bundle', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()


def digest(path, start, end=None):
    h = hashlib.sha256()
    with path.open('rb') as f:
        f.seek(start)
        left = None if end is None else end - start
        while left is None or left > 0:
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
            length, crc, blocks, count = struct.unpack_from('<IIII', header, 8)
            name = header[32:32+count].decode('ascii')
            data = f.read(length)
            if header[:8] == b'ZCFILE1\0' and (len(data) != length or zlib.crc32(data) != crc):
                raise RuntimeError('invalid candidate data: ' + name)
            result[name] = None if header[:8] == b'ZCDEL1\0\0' else data
            pos += 1 + blocks
    return result


with tempfile.TemporaryDirectory(prefix='guest-source-stage-') as name:
    temp = pathlib.Path(name)
    disk, prepared, reference, var, baseline = (temp / n for n in
        ('disk.img', 'prepared.img', 'reference.img', 'vars.fd', 'baseline.tar'))
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                        '::/boot/sources.tar', str(baseline)], check=True)
        shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
        log = ROOT / f'build/utm-parity-goal/guest-bank-{label}-setup.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        guest = check.Guest(check.build_command(temp, var, disk, None), log)
        try:
            check.boot_to_prompt(guest, label + ' setup')
            guest.command('zput Tests/StageUser.ZC I64 StageUser(){return 719;}', 'zput: saved Tests/StageUser.ZC')
            guest.command('zput Demo/Graphics/Lines.ZC I64 StageEdited(){return 613;}',
                          'zput: saved Demo/Graphics/Lines.ZC')
            guest.command('zrm Demo/Graphics/Clock.ZC', 'zrm: deleted Demo/Graphics/Clock.ZC')
        finally:
            guest.close()
        shutil.copyfile(disk, prepared)
        shutil.copyfile(disk, reference)
        subprocess.run([sys.executable, str(ROOT / 'scripts/source-volume.py'), 'merge-baseline',
                        str(reference), str(baseline), str(args.bundle)], check=True)
        sectors = disk.stat().st_size // 512
        inactive = 196575 if sectors == 262144 else sectors - 33 - 65536
        expected = records(reference, inactive)
        before = (digest(prepared, 0, inactive * 512), digest(prepared, (inactive + 65536) * 512))
        for mode in ('stage-bank', 'interrupt-bank'):
            shutil.copyfile(prepared, disk)
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            log = ROOT / f'build/utm-parity-goal/guest-bank-{label}-{mode}.log'
            guest = check.Guest(check.build_command(temp, var, disk, args.diagnostic_iso.resolve()), log)
            try:
                check.boot_to_prompt(guest, f'{label} {mode}')
                expected_output = ('diagnostic source bank staged' if mode == 'stage-bank' else
                                   'diagnostic source staging interrupted')
                guest.command('zinstall journal-test ' + mode, expected_output, timeout=900)
            finally:
                guest.close()
            with disk.open('rb') as f:
                f.seek(inactive * 512)
                if any(f.read(512)):
                    raise RuntimeError('candidate source bank published prematurely')
            if mode == 'stage-bank' and records(disk, inactive) != expected:
                raise RuntimeError('guest candidate differs from baseline-aware reference')
            if before != (digest(disk, 0, inactive * 512), digest(disk, (inactive + 65536) * 512)):
                raise RuntimeError('staging changed bytes outside inactive bank')
            shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
            log = ROOT / f'build/utm-parity-goal/guest-bank-{label}-{mode}-verify.log'
            guest = check.Guest(check.build_command(temp, var, disk, None), log)
            try:
                check.boot_to_prompt(guest, f'{label} {mode} verify')
                guest.command('zload disk:Tests/StageUser.ZC', 'zc: loaded disk:Tests/StageUser.ZC')
                guest.command('zcall StageUser', 'zc => 0x00000000000002cf')
                guest.command('zload disk:Demo/Graphics/Lines.ZC', 'zc: loaded disk:Demo/Graphics/Lines.ZC')
                guest.command('zcall StageEdited', 'zc => 0x0000000000000265')
                guest.command('zload disk:Demo/Graphics/Clock.ZC', 'source not found')
            finally:
                guest.close()
            print(f'PASS {label}: {mode}, active bank and user changes preserved', flush=True)
        for path in (disk, prepared, reference, var, baseline):
            path.unlink()
