#!/usr/bin/env python3
"""Check package structure/version and signed-file digests before artifact upload.
The Tizen signer performs signing. This check does not establish Samsung TV trust.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import base64
import hashlib
import re
from pathlib import PurePosixPath
import xml.etree.ElementTree as ET
from urllib.parse import unquote
from zipfile import ZipFile

DS={'ds':'http://www.w3.org/2000/09/xmldsig#'}

def verify(path,version):
    with ZipFile(path) as archive:
        names=archive.namelist()
        if sum(item.file_size for item in archive.infolist()) > 64 * 1024 * 1024:
            raise ValueError('Web UI archive exceeds the 64 MiB validation limit')
        if len(names)!=len(set(names)): raise ValueError('Duplicate archive entries')
        for name in names:
            p=PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name: raise ValueError('Unsafe archive path')
        for name in ('config.xml','index.html','js/app.js','js/native-bridge.js','icon.png','author-signature.xml','signature1.xml'):
            if name not in names: raise ValueError('Missing package entry: '+name)
        if ET.fromstring(archive.read('config.xml')).get('version')!=version: raise ValueError('Version mismatch')
        covered=set()
        for name in ('author-signature.xml','signature1.xml'):
            signature=ET.fromstring(archive.read(name))
            refs=signature.findall('./ds:SignedInfo/ds:Reference',DS)
            if not refs or signature.find('./ds:SignatureValue',DS) is None: raise ValueError('Missing signature data')
            for ref in refs:
                uri=unquote(ref.get('URI',''))
                if uri.startswith('#'): continue  # Signature properties, verified by the signing tool.
                if not uri or uri not in names: raise ValueError('Invalid signature reference')
                algorithm=ref.find('ds:DigestMethod',DS).get('Algorithm','')
                algorithms={'http://www.w3.org/2001/04/xmlenc#sha512':'sha512','http://www.w3.org/2001/04/xmlenc#sha256':'sha256','http://www.w3.org/2000/09/xmldsig#sha1':'sha1'}
                if algorithm not in algorithms: raise ValueError('Unsupported digest algorithm')
                actual=hashlib.new(algorithms[algorithm],archive.read(uri)).digest()
                expected=base64.b64decode(''.join(ref.findtext('ds:DigestValue',default='',namespaces=DS).split()),validate=True)
                if actual!=expected: raise ValueError('Signed file digest mismatch: '+uri)
                if name=='author-signature.xml':covered.add(uri)
        payload={name for name in names if not name.endswith('/') and not re.fullmatch(r'signature[1-9][0-9]*\.xml',name) and name!='author-signature.xml'}
        if not payload.issubset(covered): raise ValueError('Unsigned files in WGT: '+str(sorted(payload-covered)))
    print('Signed WGT structure, payload digests and version verified')

def verify_crypto(path,jdk=None):
    root=jdk or (str(Path(os.environ['TIZEN_STUDIO_ROOT'])/'jdk') if os.environ.get('TIZEN_STUDIO_ROOT') else None)
    compiler=str(Path(root)/'bin/javac') if root else shutil.which('javac')
    runtime=str(Path(root)/'bin/java') if root else shutil.which('java')
    if not compiler or not runtime:raise ValueError('A JDK is required for cryptographic WGT verification; set TIZEN_STUDIO_ROOT or --jdk')
    with tempfile.TemporaryDirectory(prefix='airtizen-signature-') as directory:
        subprocess.run([compiler,'-d',directory,str(Path(__file__).with_name('VerifyWgtSignature.java'))],check=True)
        subprocess.run([runtime,'-cp',directory,'VerifyWgtSignature',str(Path(path).resolve())],check=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('package');parser.add_argument('version');parser.add_argument('--jdk')
    args=parser.parse_args();verify(args.package,args.version);verify_crypto(args.package,args.jdk)
