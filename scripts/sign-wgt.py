#!/usr/bin/env python3
"""Run the Tizen signer with a real terminal, bounded prompts and redacted output."""
import argparse
import errno
import os
from pathlib import Path
import pty
import re
import select
import signal
import sys
import time

def sign(executable,build,output,profile,timeout=120):
    author=os.environ.get('TIZEN_CERT_PASSWORD','')
    distributor=os.environ.get('TIZEN_DISTRIBUTOR_PASSWORD','')
    if not author or not distributor:raise ValueError('Both signing passwords must be set in the environment')
    for password in (author,distributor):
        if '\n' in password or '\r' in password:raise ValueError('Signing passwords cannot contain terminal line breaks')
    output=Path(output)
    output.mkdir(parents=True,exist_ok=True)
    if list(output.glob('*.wgt')):raise ValueError('Signing output already contains a WGT; choose a fresh output directory')
    if any((Path(build)/name).exists() for name in ('author-signature.xml','signature1.xml')):raise ValueError('Signing input already contains signatures; rebuild a fresh staged UI')
    pid,fd=pty.fork()
    if pid==0:
        os.execv(str(executable),[str(executable),'package','-t','wgt','-s',profile,'-o',str(output),'--',str(build)])
    captured=bytearray();position=0;deadline=time.monotonic()+timeout;timed_out=False
    prompts=[(re.compile(rb'author password:'),author),(re.compile(rb'distributor[0-9]* password:'),distributor),(re.compile(rb'certificate password:'),author),(re.compile(rb'yes: \(y\), no: \(n\) \?'),'N')]
    attempts={}
    try:
        while True:
            if time.monotonic()>deadline:
                timed_out=True;os.killpg(pid,signal.SIGTERM);break
            ready,_,_=select.select([fd],[],[],.2)
            if not ready:continue
            try:block=os.read(fd,4096)
            except OSError as error:
                if error.errno==errno.EIO:break
                raise
            if not block:break
            captured.extend(block)
            if len(captured)>1024*1024:raise RuntimeError('Tizen signer output exceeded its bound')
            pending=bytes(captured[position:]).lower()
            for marker,password in prompts:
                match=marker.search(pending)
                if match:
                    attempts[marker.pattern]=attempts.get(marker.pattern,0)+1
                    if attempts[marker.pattern]>3:raise RuntimeError('Signing password prompt repeated more than three times')
                    os.write(fd,password.encode()+b'\n');position+=match.end();break
    except BaseException:
        try:os.killpg(pid,signal.SIGTERM)
        except ProcessLookupError:pass
        raise
    finally:
        os.close(fd)
        _,status=os.waitpid(pid,0)
    text=captured.decode(errors='replace')
    for password in (author,distributor):text=text.replace(password,'[REDACTED]')
    sys.stdout.write(text)
    if timed_out:return 124
    return os.waitstatus_to_exitcode(status)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('build',type=Path);parser.add_argument('output',type=Path);parser.add_argument('profile')
    args=parser.parse_args();raise SystemExit(sign(args.executable,args.build,args.output,args.profile))
