#!/usr/bin/env python3
"""Copy UI to a separate build directory; stamp only that disposable copy."""
import argparse
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]

def stage(destination,version):
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
    return destination

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('destination');parser.add_argument('version')
    args=parser.parse_args();print(stage(args.destination,args.version))
