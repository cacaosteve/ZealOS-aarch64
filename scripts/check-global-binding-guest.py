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
with tempfile.TemporaryDirectory(prefix="global-binding-") as name:
 temp=pathlib.Path(name); disk=temp/"disk.img"; var=temp/"vars.fd"
 subprocess.run(["/bin/cp","-c",str(a.upgraded.resolve()),str(disk)],check=True)
 subprocess.run(["mcopy","-o","-i",str(disk)+"@@1048576",str(a.kernel.resolve()),"::/ZVKERNEL.ELF"],check=True)
 shutil.copyfile("/opt/homebrew/share/qemu/edk2-arm-vars.fd",var)
 guest=check.Guest(check.build_command(temp,var,disk,None),ROOT/"build/utm-parity-goal/checkpoint-global-serial.log")
 try:
  check.boot_to_prompt(guest,"global binding")
  guest.command('zc I64 binding_prior=41; I64 *binding_saved=&binding_prior;',"zc: executed",timeout=180)
  guest.command('zc F64 binding_prior=1.0; Print("binding_prior_ok=%d\\n",*binding_saved==41);',"binding_prior_ok=1",timeout=180)
  guest.command('zc extern I64 binding_deferred;',"zc: executed",timeout=180)
  guest.command('zc I64 binding_deferred=42;',"zc: executed",timeout=180)
  guest.command('zc I64 BindingDeferredRead(){return binding_deferred;}',"zc: executed",timeout=180)
  guest.command('zcall BindingDeferredRead',"zc => 0x000000000000002a",timeout=180)
  print("PASS incompatible redefinition and deferred compatible extern",flush=True)
 finally:guest.close()
