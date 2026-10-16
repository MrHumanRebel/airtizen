#!/usr/bin/env python3
"""Copy UI to a separate build directory; stamp only that disposable copy."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]

def stage(destination,version,receiver=None):
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',version):
        raise ValueError('Expected numeric major.minor.patch version')
    if any(int(p)>65535 for p in version.split('.')): raise ValueError('Version exceeds Tizen limits')
    destination=Path(destination).resolve()
    if destination.exists(): raise ValueError('Staging destination already exists; choose an empty directory')
    # Never stage inside the source app or recursively copy generated release directories.
    if destination==ROOT/'app' or ROOT/'app' in destination.parents: raise ValueError('Stage outside app/')
    shutil.copytree(ROOT/'app',destination,ignore=shutil.ignore_patterns('.buildResult','release','node_modules','*.wgt'))
    manifest=destination/'config.xml'
    source=manifest.read_text()
    source,count=re.subn(r'version="[0-9]+\.[0-9]+\.[0-9]+"',lambda _: 'version="'+version+'"',source,count=1)
    if count!=1: raise ValueError('Missing widget version')
    manifest.write_text(source)
    page=destination/'index.html'
    page.write_text(re.sub(r'(<span id="app-version">)[^<]+',lambda m:m[1]+version,page.read_text()))
    ET.parse(manifest)
    if receiver is not None:
        spec=importlib.util.spec_from_file_location('embedded',ROOT/'scripts/embedded-receiver.py')
        embedded=importlib.util.module_from_spec(spec);spec.loader.exec_module(embedded)
        data=Path(receiver).read_bytes();embedded.check_module(data)
        module_dir=destination/'receiver';module_dir.mkdir()
        (module_dir/'receiver_arm.nexe').write_bytes(data)
        (module_dir/'receiver.nmf').write_text(json.dumps({'program':{'arm':{'url':'receiver_arm.nexe'}}},indent=2)+'\n')
        licenses=destination/'licenses';licenses.mkdir(exist_ok=True)
        shutil.copy2(ROOT/'THIRD_PARTY_NOTICES.md',licenses/'THIRD_PARTY_NOTICES.md')
        shutil.copy2(ROOT/'third_party/AirPlayServerLib/lib/playfair/LICENSE.md',licenses/'COPYING.GPLv3')
        cache=Path(os.environ.get('AIRTIZEN_DEPS_DIR',ROOT.parent/'.airtizen-deps'))
        sdk=Path(os.environ.get('NACL_SDK_ROOT',cache/'nacl-sdk/pepper_47'))
        ffmpeg=cache/'nacl-media-source/FFmpeg-3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587'
        for name in ('LICENSE','COPYING','NOTICE'):
            shutil.copy2(sdk/name,licenses/('NaCl-'+name+'.txt'))
        for name in ('COPYING.LGPLv2.1','LICENSE.md'):
            shutil.copy2(ffmpeg/name,licenses/('FFmpeg-'+name))
        embedded.check_embedded(lambda name:(destination/name).read_bytes())
    return destination

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('destination');parser.add_argument('version');parser.add_argument('--receiver',type=Path)
    args=parser.parse_args();print(stage(args.destination,args.version,args.receiver))
