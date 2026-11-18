#!/usr/bin/env python3
"""Reproduce the preview's Pi overlays and patched Limine EFI binary."""
import argparse
import hashlib
import pathlib
import subprocess
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'build/arm64-preview-1'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--release-dir', type=pathlib.Path, default=DEFAULT)
p.add_argument('--llvm-dir', type=pathlib.Path, default=pathlib.Path('/opt/homebrew/opt/llvm/bin'))
p.add_argument('--lld', default='/opt/homebrew/bin/ld.lld')
a = p.parse_args()
r = a.release_dir.resolve()
firmware = r / 'sources/RPi4_UEFI_Firmware_v1.53.zip'
linux = r / 'sources/raspberrypi-linux-4ce59edc8d.tar.gz'
limine = r / 'sources/limine-12.9.0-upstream.tar.gz'
pi = r / 'zealos-arm64-preview-1-pi4b.img'
iso = r / 'zealos-arm64-preview-1-utm.iso'

def run(command, **kw):
    subprocess.run([str(x) for x in command], check=True, **kw)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()

assert digest(firmware) == 'ca9973e2a7a546b3df871cfb7382e656829114b6dfa424f40dc67cc90a217d88', 'Unexpected firmware archive'
with tempfile.TemporaryDirectory(prefix='preview-provenance-') as name:
    t = pathlib.Path(name)
    with zipfile.ZipFile(firmware) as z:
        files = [n for n in z.namelist() if not n.endswith('/')]
        for n in files:
            target = t / 'fat-file'
            run(['mcopy', '-i', str(pi) + '@@1048576', '::/' + n, target], stdout=subprocess.DEVNULL)
            assert target.read_bytes() == z.read(n), n
            target.unlink()
        print(f'PASS {len(files)} Pi firmware image files match official v1.53 archive', flush=True)
        linux_tree = 'linux-4ce59edc8d949410d322873b35767e95a5ce5822'
        source = t / 'linux'
        source.mkdir()
        run(['tar', '-xzf', linux, '-C', source, '--strip-components=1',
             linux_tree + '/arch/arm/boot/dts/overlays', linux_tree + '/include/dt-bindings'])
        for n in ('miniuart-bt', 'upstream-pi4'):
            dts = t / (n + '.dts')
            dtbo = t / (n + '.dtbo')
            run([a.llvm_dir / 'clang', '-E', '-nostdinc', '-undef', '-D__DTS__',
                 '-x', 'assembler-with-cpp', '-I', source / 'include', '-I',
                 source / 'arch/arm/boot/dts', source / ('arch/arm/boot/dts/overlays/' + n + '-overlay.dts'), '-o', dts])
            run(['dtc', '-@', '-H', 'epapr', '-I', 'dts', '-O', 'dtb', '-o', dtbo, dts], stderr=subprocess.DEVNULL)
            assert dtbo.read_bytes() == z.read('overlays/' + n + '.dtbo'), n
            print(f'PASS {n} overlay reproduced: {digest(dtbo)}', flush=True)
    run(['tar', '-xzf', limine, '-C', t])
    l = t / 'limine-12.9.0'
    run(['patch', '-p1', '-i', ROOT / 'patches/limine-12.9.0-qemu-virt-pl011-hhdm.patch'], cwd=l, stdout=subprocess.DEVNULL)
    options = ['--enable-uefi-aarch64', '--enable-uefi-cd',
               f'CC_FOR_TARGET={a.llvm_dir}/clang --target=aarch64-none-elf',
               f'LD_FOR_TARGET={a.lld}', f'OBJCOPY_FOR_TARGET={a.llvm_dir}/llvm-objcopy',
               f'OBJDUMP_FOR_TARGET={a.llvm_dir}/llvm-objdump', f'READELF_FOR_TARGET={a.llvm_dir}/llvm-readelf']
    with (t / 'limine-build.log').open('wb') as log:
        run(['./configure', *options], cwd=l, stdout=log, stderr=subprocess.STDOUT)
        run(['gmake', '-j4'], cwd=l, stdout=log, stderr=subprocess.STDOUT)
    built = l / 'bin/BOOTAA64.EFI'
    assert digest(built) == '3f51768c5eb8a0818b8e7aa2ac9a9caef3c64707b5b6db15d1e612de23236b8c'
    run(['xorriso', '-osirrox', 'on', '-indev', iso, '-extract', '/EFI/BOOT/BOOTAA64.EFI', t / 'iso-efi',
         '-extract', '/boot/limine/limine-uefi-cd.bin', t / 'iso-cd'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(['mcopy', '-i', t / 'iso-cd', '::/EFI/BOOT/BOOTAA64.EFI', t / 'iso-cd-efi'], stdout=subprocess.DEVNULL)
    run(['mcopy', '-i', str(pi) + '@@1048576', '::/EFI/BOOT/BOOTAA64.EFI', t / 'pi-efi'], stdout=subprocess.DEVNULL)
    for entry in ('iso-efi', 'iso-cd-efi', 'pi-efi'):
        assert (t / entry).read_bytes() == built.read_bytes(), entry
        print(f'PASS {entry} matches rebuilt patched Limine: {digest(built)}', flush=True)
print('PASS preview firmware and bootloader provenance checks')
