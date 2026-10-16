#!/usr/bin/env python3
"""Exercise actual package signatures, including rejection of a changed SignatureValue."""
import importlib.util
from pathlib import Path
import re,subprocess,sys,tempfile
from zipfile import ZipFile,ZIP_DEFLATED
ROOT=Path(__file__).resolve().parents[1]

def main(package,jdk):
    spec=importlib.util.spec_from_file_location('wgt',ROOT/'scripts/verify-wgt.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.verify_crypto(package,jdk)
    with tempfile.TemporaryDirectory(prefix='airtizen-bad-signature-') as directory:
        changed=Path(directory)/'changed.wgt'
        with ZipFile(package) as original,ZipFile(changed,'w',ZIP_DEFLATED) as output:
            for item in original.infolist():
                data=original.read(item.filename)
                if item.filename=='author-signature.xml':
                    pattern=rb'(<(?:\w+:)?SignatureValue(?:\s[^>]*)?>)(.*?)(</(?:\w+:)?SignatureValue>)'
                    def corrupt(match):
                        payload=bytearray(match[2]);index=next(i for i,b in enumerate(payload) if chr(b).isalnum());payload[index]=ord('A') if payload[index]!=ord('A') else ord('B')
                        return match[1]+bytes(payload)+match[3]
                    data,count=re.subn(pattern,corrupt,data,count=1,flags=re.S)
                    if count!=1:raise AssertionError('Missing signature value in actual package')
                output.writestr(item,data)
        with open(Path(directory)/'verification.log','w') as log:
            # Check RSA itself; a version/digest failure must not mask this regression.
            try:
                subprocess.run([str(Path(jdk)/'bin/javac'),'-d',directory,str(ROOT/'scripts/VerifyWgtSignature.java')],check=True,stdout=log,stderr=log)
                result=subprocess.run([str(Path(jdk)/'bin/java'),'-cp',directory,'VerifyWgtSignature',str(changed)],stdout=log,stderr=log)
            except OSError as error:raise AssertionError('JDK verification could not execute') from error
        if result.returncode==0:raise AssertionError('Modified RSA signature was accepted')
    print('Actual WGT signatures passed; a changed RSA signature was rejected')
if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('usage: test_wgt_crypto.py PACKAGE JDK')
    main(Path(sys.argv[1]),Path(sys.argv[2]))
