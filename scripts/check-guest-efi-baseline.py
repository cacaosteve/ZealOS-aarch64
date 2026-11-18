#!/usr/bin/env python3
"""Verify the read-only EFI baseline reader on disposable installs."""
import argparse
import importlib.util
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('installer_check', ROOT / 'scripts/check-iso-installer.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('diagnostic_iso', type=pathlib.Path)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='guest-efi-baseline-') as name:
    temp = pathlib.Path(name)
    disk, var, reference, output, executable = (temp / n for n in
        ('disk.img', 'vars.fd', 'reference.tar', 'output.tar', 'test'))
    subprocess.run(['cc', '-std=c11', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_read_test.c'),
                    '-o', str(executable)], check=True)
    tar_test = temp / 'tar-test'
    subprocess.run(['cc', '-std=c11', '-D_FILE_OFFSET_BITS=64',
                    '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', str(ROOT / 'tests/efi_source_tar_test.c'),
                    '-o', str(tar_test)], check=True)
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        shutil.copyfile(source, disk)
        before = check.file_digest(disk)
        subprocess.run(['mcopy', '-i', str(disk) + '@@1048576',
                        '::/boot/sources.tar', str(reference)], check=True)
        subprocess.run([str(executable), str(disk), str(output)], check=True)
        if check.file_digest(output) != check.file_digest(reference):
            raise RuntimeError('native reader differs from independent FAT extraction')
        data = reference.read_bytes()
        expected = f'diagnostic baseline bytes {len(data)} crc32 {zlib.crc32(data)}'
        with tarfile.open(reference) as archive:
            entries = [(m.name, m.size, zlib.crc32(archive.extractfile(m).read()))
                       for m in archive if m.isfile()]
        manifest = subprocess.check_output([str(tar_test), str(disk)], text=True).splitlines()
        if manifest != [f'{path}\t{size}\t{crc}' for path, size, crc in entries]:
            raise RuntimeError('streamed tar entries differ from independent reader')
        expected_index = (f'diagnostic baseline files {len(entries)} bytes '
                          f'{sum(size for _, size, _ in entries)} crc-sum '
                          f'{sum(crc for _, _, crc in entries) & 0xffffffff}')
        shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd', var)
        log = ROOT / f'build/utm-parity-goal/baseline-reader-{label}.log'
        log.parent.mkdir(parents=True, exist_ok=True)
        guest = check.Guest(check.build_command(temp, var, disk, args.diagnostic_iso.resolve()), log)
        try:
            check.boot_to_prompt(guest, f'{label} baseline')
            guest.command('zinstall journal-test baseline', expected, timeout=900)
            guest.command('zinstall journal-test baseline-index', expected_index, timeout=900)
        finally:
            guest.close()
        if check.file_digest(disk) != before:
            raise RuntimeError('read-only baseline check modified disk')
        print(f'PASS {label}: baseline read and unchanged target', flush=True)
        for path in (disk, var, reference, output):
            path.unlink()
