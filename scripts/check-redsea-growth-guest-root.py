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
with tempfile.TemporaryDirectory(prefix="growth-root-") as name:
 temp=pathlib.Path(name); disk=temp/"disk.img"; var=temp/"vars.fd"
 for label,source in (("128m",a.legacy),("1g",a.upgraded)):
  subprocess.run(["/bin/cp","-c",str(source.resolve()),str(disk)],check=True)
  if label=="128m":legacy_seed(disk)
  else:subprocess.run(["mcopy","-o","-i",str(disk)+"@@1048576",str(a.kernel.resolve()),"::/ZVKERNEL.ELF"],check=True)
  shutil.copyfile("/opt/homebrew/share/qemu/edk2-arm-vars.fd",var)
  log=ROOT/f"build/utm-parity-goal/guest-root-isolated-{label}.log"
  guest=check.Guest(check.build_command(temp,var,disk,None),log)
  try:
   check.boot_to_prompt(guest,f"{label} root growth setup")
   for i in range(6):
    guest.command(f'zc Print("root_seed=%d\\n",FileWrite("disk:/ROOT{i}.TXT","alive",5)>=0);',"root_seed=1",timeout=180)
  finally:guest.close()
  if b"RedSea: directory grew safely" not in log.read_bytes():raise RuntimeError(f"{label}: no root growth recorded")
  shutil.copyfile("/opt/homebrew/share/qemu/edk2-arm-vars.fd",var)
  guest=check.Guest(check.build_command(temp,var,disk,None),log.with_name(log.stem+"-reboot.log"))
  try:
   check.boot_to_prompt(guest,f"{label} root growth reboot")
   guest.command('zc I64 n=-1; U8 *p=FileRead("disk:/Home/Hello.TXT",&n); Print("root_hello=%d\\n",p!=NULL && n==15 && p[0]==97 && p[14]==5); Free(p);',"root_hello=1",timeout=180)
   for i in range(6):
    guest.command(f'zc U8 *v{i}=FileRead("disk:/ROOT{i}.TXT"); Print("root_file=%d\\n",v{i}!=NULL && !StrCompare(v{i},"alive")); Free(v{i});',"root_file=1",timeout=180)
  finally:guest.close()
  disk.unlink();print(f"PASS {label}: root growth and all files after reboot",flush=True)
