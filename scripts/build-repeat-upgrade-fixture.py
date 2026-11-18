#!/usr/bin/env python3
"""Create a disposable ISO whose source archive has one extra test file."""
import argparse
import io
import pathlib
import subprocess
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='repeat-iso-fixture-') as name:
    root = pathlib.Path(name) / 'iso'
    subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(args.base.resolve()), '-extract', '/', str(root)],
                   check=True, stdout=subprocess.DEVNULL)
    tar_path = root / 'boot/sources.tar'
    tar_path.chmod(0o644)
    (root / 'boot/upgrade-sources.lz4').chmod(0o644)
    data = b'I64 RepeatNew(){return 931;}\n'
    with tarfile.open(tar_path, 'a', format=tarfile.USTAR_FORMAT) as archive:
        entry = tarfile.TarInfo('Tests/RepeatNew.ZC'); entry.size = len(data); entry.mode = 0o644
        archive.addfile(entry, io.BytesIO(data))
    subprocess.run(['python3', str(ROOT / 'scripts/lz4-source-archive.py'), str(tar_path),
                    str(root / 'boot/upgrade-sources.lz4')], check=True)
    subprocess.run(['xorriso', '-as', 'mkisofs', '-R', '-r', '-J', '-hfsplus', '-apm-block-size', '2048',
                    '--efi-boot', 'boot/limine/limine-uefi-cd.bin', '--efi-boot-part', '--efi-boot-image',
                    '--protective-msdos-label', str(root), '-o', str(args.output.resolve())], check=True,
                   stdout=subprocess.DEVNULL)
