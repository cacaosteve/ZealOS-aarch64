#!/usr/bin/env python3
"""Exercise experimental reclaim cursor on disposable disk copies."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import hashlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('legacy', type=pathlib.Path)
parser.add_argument('expanded', type=pathlib.Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='efi-reclaim-') as name:
    tmp = pathlib.Path(name)
    exe, disk, esp, prepared, config = (tmp / n for n in ('test', 'disk.img', 'esp.img', 'prepared.img', 'limine.conf'))
    subprocess.run(['cc', '-O2', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-function', str(ROOT / 'tests/efi_fat32_reclaim_test.c'),
                    '-o', str(exe)], check=True)
    def run(action, fail=0):
        return subprocess.run([str(exe), str(disk), action, str(fail)], check=True,
                              capture_output=True, text=True).stdout
    def non_efi_hash(path):
        h = hashlib.sha256()
        with path.open('rb') as f:
            h.update(f.read(34 * 512))
            f.seek(130911 * 512)
            for chunk in iter(lambda: f.read(1024 * 1024), b''):
                h.update(chunk)
        return h.digest()
    def full_hash(path):
        h = hashlib.sha256()
        with path.open('rb') as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b''):
                h.update(chunk)
        return h.digest()
    for label, source in (('128m', args.legacy), ('1g', args.expanded)):
        expected = non_efi_hash(source)
        shutil.copyfile(source, disk)
        before = full_hash(disk)
        rejected = subprocess.run([str(exe), str(disk), 'start', '0'], capture_output=True)
        if rejected.returncode == 0 or full_hash(disk) != before:
            raise RuntimeError('unsafe old boot configuration was not rejected unchanged')
        shutil.copyfile(source, prepared)
        subprocess.run(['mcopy', '-i', str(prepared) + '@@1048576',
                        str(ROOT / 'build/upgrade-sources.lz4'), '::/ZUSOURCE.LZ4'], check=True)
        for src, dst in (('build/kernel.elf', '::/ZUKERNEL.ELF'),
                         ('build/zealbooter.elf', '::/ZUBOOTER.ELF')):
            subprocess.run(['mcopy', '-i', str(prepared) + '@@1048576', str(ROOT / src), dst], check=True)
        current = ('timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n'
                   '/ZealOS aarch64 zealbooter\n    protocol: limine\n'
                   '    path: boot():/ZUBOOTER.ELF\n    module_path: boot():/ZUKERNEL.ELF\n'
                   '    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/ZUSOURCE.LZ4\n'
                   '    kaslr: no\n    resolution: 800x600x32\n')
        old_config = subprocess.run(['mtype', '-i', str(prepared) + '@@1048576', '::/limine.conf'],
                                    stdout=subprocess.PIPE, check=True).stdout
        config.write_bytes(current.encode().ljust(len(old_config), b'\n'))
        subprocess.run(['mcopy', '-o', '-i', str(prepared) + '@@1048576', str(config), '::/limine.conf'], check=True)
        # A damaged compressed archive must prevent reclaim without any writes.
        shutil.copyfile(prepared, disk)
        with disk.open('r+b') as f:
            f.seek(2048 * 512)
            boot = f.read(512)
            spc = boot[13]
            reserved = int.from_bytes(boot[14:16], 'little')
            fatsz = int.from_bytes(boot[36:40], 'little')
            root = int.from_bytes(boot[44:48], 'little')
            data = 2048 + reserved + 2 * fatsz
            f.seek((data + (root - 2) * spc) * 512)
            directory = f.read(spc * 512)
            entry = next(directory[i:i+32] for i in range(0, len(directory), 32)
                         if directory[i:i+11] == b'ZUSOURCELZ4')
            cluster = int.from_bytes(entry[20:22], 'little') * 65536 + int.from_bytes(entry[26:28], 'little')
            f.seek((data + (cluster - 2) * spc) * 512 + 16)
            old = f.read(1)
            f.seek(-1, 1); f.write(bytes([old[0] ^ 1]))
        before = full_hash(disk)
        rejected = subprocess.run([str(exe), str(disk), 'start', '0'], capture_output=True)
        if rejected.returncode == 0 or full_hash(disk) != before:
            raise RuntimeError('damaged compressed baseline was not rejected unchanged')
        packed = hashlib.sha256((ROOT / 'build/upgrade-sources.lz4').read_bytes()).digest()
        old_archive = subprocess.run(['mtype', '-i', str(prepared) + '@@1048576', '::/boot/sources.tar'],
                                     stdout=subprocess.PIPE, check=True).stdout
        with prepared.open('rb') as f:
            f.seek(2048 * 512 + 13)
            cluster_bytes = f.read(1)[0] * 512
        expected_clusters = (len(old_archive) + cluster_bytes - 1) // cluster_bytes
        cases = [('step', n) for n in (0, 1, 1303, 1320, 1800, 2900)]
        cases += [('later-step', n) for n in (1303, 1320, 1800, 2900)]
        cases += [('start', n) for n in (1, 1302, 1303, 1305, 1307, 1308, 1309, 1310)]
        for phase, failure in cases:
            shutil.copyfile(prepared, disk)
            initial_space = int(run('space').split('space=')[1].splitlines()[0])
            if phase == 'start':
                result = run('start', failure)
                if 'INTERRUPTED' not in result:
                    raise RuntimeError(f'start failure {failure} was not reached: {result}')
                run('recover')
                status = subprocess.run([str(exe), str(disk), 'status', '0'], capture_output=True)
                if status.returncode:
                    run('start')
            else:
                run('start')
                if phase == 'later-step':
                    run('step')
                    run('step')
            if failure:
                if phase in ('step', 'later-step'):
                    result = run('step', failure)
                    if 'INTERRUPTED' not in result:
                        raise RuntimeError(f'failure {failure} was not reached: {result}')
                    run('recover')
            for i in range(16):
                if 'done=1' in run('step'):
                    break
            else:
                raise RuntimeError('reclamation did not finish')
            final_space = int(run('space').split('space=')[1].splitlines()[0])
            if final_space - initial_space != expected_clusters:
                raise RuntimeError('unexpected number of freed clusters')
            with disk.open('rb') as src, esp.open('wb') as dst:
                src.seek(2048 * 512)
                dst.write(src.read(128863 * 512))
            subprocess.run(['fsck.fat', '-n', str(esp)], stdout=subprocess.DEVNULL, check=True)
            subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/limine.conf'],
                           stdout=subprocess.DEVNULL, check=True)
            copied = subprocess.run(['mtype', '-i', str(disk) + '@@1048576', '::/ZUSOURCE.LZ4'],
                                    stdout=subprocess.PIPE, check=True).stdout
            if hashlib.sha256(copied).digest() != packed:
                raise RuntimeError('compressed boot archive changed during reclaim')
            if non_efi_hash(disk) != expected:
                raise RuntimeError('reclaim changed bytes outside EFI and journal')
            print(f'PASS {label}: reclaim and recovery at {phase} write {failure}', flush=True)
