#!/usr/bin/env python3
"""Deterministic numeric Tizen version from reachable SemVer tags and commit distance."""
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PATTERN = re.compile(r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z')

def git(*args):
    return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()

def calculate(seed, tags, distance):
    """Tags are (version, ref) for ancestors of HEAD, sorted numerically."""
    candidates = [(tuple(map(int,PATTERN.fullmatch(v).groups())),ref) for v,ref in tags if PATTERN.fullmatch(v)]
    baseline = tuple(map(int,seed.split('.')))
    valid = [(version,ref) for version,ref in candidates if version >= baseline]
    if valid:
        version,ref=max(valid)
        steps=distance(ref)
    else:
        version,steps=baseline,distance(None)
    result=(version[0],version[1],version[2]+steps)
    if any(part>65535 for part in result):
        raise ValueError('Tizen version component exceeds 65535; bump the base major/minor version')
    return '.'.join(map(str,result))

def main():
    if git('rev-parse','--is-shallow-repository')=='true':
        raise SystemExit('Versioning requires full history and tags (checkout fetch-depth: 0)')
    seed=ET.parse(ROOT/'app/config.xml').getroot().get('version')
    reachable=[]
    for tag in git('tag','--merged','HEAD').splitlines():
        if PATTERN.fullmatch(tag): reachable.append((tag,tag))
    def distance(ref):
        return int(git('rev-list','--first-parent','--count',(ref+'..HEAD') if ref else 'HEAD'))
    print(calculate(seed,reachable,distance))

if __name__=='__main__': main()
