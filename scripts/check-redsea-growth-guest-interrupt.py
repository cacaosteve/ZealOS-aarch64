#!/usr/bin/env python3
"""Cut power to disposable QEMU guests during a real directory growth."""
import argparse
import importlib.util
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
ROOT = pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('installer_check',ROOT/'scripts/check-iso-installer.py')
check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
sys.path.insert(0,str(ROOT/'scripts'))
from installer_input import stage, text
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('legacy',type=pathlib.Path);p.add_argument('upgraded',type=pathlib.Path);p.add_argument('kernel',type=pathlib.Path);p.add_argument('iso',type=pathlib.Path)
a=p.parse_args()
def entry(attr,name,first,size):
 b=bytearray(64);struct.pack_into('<H',b,0,attr);b[2:2+len(name)]=name;struct.pack_into('<QQ',b,40,first,size);return b
def legacy_seed(path):
 base=130911;root=base+2;home=base+3;file=base+4
 boot=bytearray(512);boot[3]=0x88;boot[510:]=b'\x55\xaa';struct.pack_into('<QQQQ',boot,8,base,128,root,1)
 bitmap=bytearray(512);bitmap[0]=15
 r=bytearray(512);r[:64]=entry(0x410,b'.',root,512);r[64:128]=entry(0x410,b'..',root,0);r[128:192]=entry(0x410,b'Home',home,512)
 h=bytearray(512);h[:64]=entry(0x410,b'.',home,512);h[64:128]=entry(0x410,b'..',root,0);h[128:192]=entry(0x400,b'Hello.TXT',file,15)
 with path.open('r+b') as f:
  for lba,data in ((base,boot),(base+1,bitmap),(root,r),(home,h),(file,b'alskdjflasdkjf\x05'.ljust(512,b'\0'))):
   f.seek(lba*512);f.write(data)
with tempfile.TemporaryDirectory(prefix='growth-power-cut-') as name:
 temp=pathlib.Path(name); disk=temp/'disk.img'; var=temp/'vars.fd'
 for label,source in (('128m',a.legacy),('1g',a.upgraded)):
  subprocess.run(['/bin/cp','-c',str(source.resolve()),str(disk)],check=True)
  if label=='128m':legacy_seed(disk)
  else:
   original=temp/'original-kernel.elf'
   subprocess.run(['mcopy','-i',str(disk)+'@@1048576','::/ZVKERNEL.ELF',str(original)],check=True)
   subprocess.run(['mcopy','-o','-i',str(disk)+'@@1048576',str(a.kernel.resolve()),'::/ZVKERNEL.ELF'],check=True)
  shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd',var)
  log=ROOT/f'build/utm-parity-goal/guest-power-cut-{label}.log'
  guest=check.Guest(check.build_command(temp,var,disk,None),log)
  try:
   check.boot_to_prompt(guest,f'{label} growth setup')
   guest.command('zc Print("growth_dir=%d\\n",DirMake("disk:/Home/GROW"));', 'growth_dir=1', timeout=180)
   for i in range(6):
    guest.command(f'zc Print("growth_seed=%d\\n",FileWrite("disk:/Home/GROW/F{i}.TXT","alive",5)>=0);',
                  'growth_seed=1',timeout=180)
   outcome=[]
   def run_write():
    try: outcome.append(guest.command('zc FileWrite("disk:/Home/GROW/NEW.TXT","new",3);','zc: executed',timeout=180))
    except Exception as e:outcome.append(e)
   thread=threading.Thread(target=run_write,daemon=True);thread.start()
   fd=os.open(disk,os.O_RDONLY);deadline=time.monotonic()+120;hit=False
   try:
    while time.monotonic()<deadline and guest.process.poll() is None:
     marker=os.pread(fd,8,1996*512);header=os.pread(fd,8,34*512);record=os.pread(fd,8,35*512)
     if marker==b'RSGTXN01' and header==b'ZUJRNL02' and record==b'ZUOLD001':
      hit=True;guest.process.kill();break
     if not thread.is_alive():break
     time.sleep(.001)
   finally:os.close(fd)
   if not hit:raise RuntimeError(f'{label}: missed live journal write; outcome={outcome!r}')
   guest.process.wait(timeout=10);thread.join(timeout=10)
  finally:guest.close()
  shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd',var)
  guest=check.Guest(check.build_command(temp,var,disk,None),log.with_name(log.stem+'-reboot.log'))
  try:
   boot=check.boot_to_prompt(guest,f'{label} after power cut')
   if b'RedSea: directory recovery completed' not in boot:raise RuntimeError('recovery marker missing')
   guest.command('zc I64 n=-1; U8 *p=FileRead("disk:/Home/Hello.TXT",&n); '
     'Print("power_cut_hello=%d\\n",p!=NULL && n==15 && p[0]==97 && p[14]==5); Free(p);',
     'power_cut_hello=1',timeout=180)
   for i in range(6):
    guest.command(f'zc U8 *v{i}=FileRead("disk:/Home/GROW/F{i}.TXT"); '
      f'Print("power_cut_file=%d\\n",v{i}!=NULL && !StrCompare(v{i},"alive")); Free(v{i});',
      'power_cut_file=1',timeout=180)

  finally:guest.close()
  print(f'PASS {label}: killed guest during journal write; recovery and original files passed',flush=True)
  # Restore the fingerprinted boot kernel on the upgraded fixture before
  # testing the installer. The candidate kernel was only for the prior boot.
  if label=='1g':
   subprocess.run(['mcopy','-o','-i',str(disk)+'@@1048576',str(original),'::/ZVKERNEL.ELF'],check=True)
  shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd',var)
  guest=check.Guest(check.build_command(temp,var,disk,a.iso.resolve()),log.with_name(log.stem+'-upgrade.log'))
  try:
   check.boot_to_prompt(guest,f'{label} post-recovery installer')
   guest.command('zc I64 InstallerAcceptanceStage(){return bootstrap_gui_install_stage;}','zc: executed')
   stage(guest,5);text(guest.qmp_path,'y');stage(guest,7,timeout=900);text(guest.qmp_path,'y')
   stage(guest,6,timeout=1800);text(guest.qmp_path,'n')
  finally:guest.close()
  shutil.copyfile('/opt/homebrew/share/qemu/edk2-arm-vars.fd',var)
  guest=check.Guest(check.build_command(temp,var,disk,None),log.with_name(log.stem+'-upgrade-reboot.log'))
  try:
   check.boot_to_prompt(guest,f'{label} post-recovery upgrade boot')
   guest.command('zc I64 n=-1; U8 *p=FileRead("disk:/Home/Hello.TXT",&n); '
     'Print("upgrade_preserved=%d\\n",p!=NULL && n==15 && p[0]==97 && p[14]==5); Free(p);',
     'upgrade_preserved=1',timeout=180)
   for i in range(6):
    guest.command(f'zc U8 *v{i}=FileRead("disk:/Home/GROW/F{i}.TXT"); '
      f'Print("upgrade_file=%d\\n",v{i}!=NULL && !StrCompare(v{i},"alive")); Free(v{i});',
      'upgrade_file=1',timeout=180)
  finally:guest.close()
  disk.unlink();print(f'PASS {label}: upgrade after interruption preserved all test files',flush=True)
