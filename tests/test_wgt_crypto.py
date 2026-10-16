#!/usr/bin/env python3
"""Verify actual WGT signatures and reject independent author/distributor tampering."""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]


def main(package, jdk, allow_sdk_distributor=False):
    spec = importlib.util.spec_from_file_location('wgt', ROOT/'scripts/verify-wgt.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.verify_crypto(package, jdk, allow_sdk_distributor)
    with tempfile.TemporaryDirectory(prefix='airtizen-bad-signature-') as directory:
        directory = Path(directory)
        with (directory/'verification.log').open('w') as log:
            subprocess.run([str(jdk/'bin/javac'), '-d', str(directory),
                            str(ROOT/'scripts/VerifyWgtSignature.java')],
                           check=True, stdout=log, stderr=log)
            command = [str(jdk/'bin/java'), '-cp', str(directory), 'VerifyWgtSignature']
            if allow_sdk_distributor:
                strict = subprocess.run(command+[str(package)], stdout=log, stderr=log)
                if strict.returncode == 0:
                    raise AssertionError('Legacy SDK certificate accepted without explicit policy')
            options = ['--allow-sdk-distributor'] if allow_sdk_distributor else []
            for target in ('author-signature.xml', 'signature1.xml'):
                changed = directory/(target+'.wgt')
                with ZipFile(package) as original, ZipFile(changed, 'w', ZIP_DEFLATED) as output:
                    for item in original.infolist():
                        data = original.read(item.filename)
                        if item.filename == target:
                            pattern = rb'(<(?:\w+:)?SignatureValue(?:\s[^>]*)?>)(.*?)(</(?:\w+:)?SignatureValue>)'
                            def corrupt(match):
                                payload = bytearray(match[2])
                                index = next(i for i, b in enumerate(payload) if chr(b).isalnum())
                                payload[index] = ord('A') if payload[index] != ord('A') else ord('B')
                                return match[1]+bytes(payload)+match[3]
                            data, count = re.subn(pattern, corrupt, data, count=1, flags=re.S)
                            if count != 1:
                                raise AssertionError('Missing actual signature value: '+target)
                        output.writestr(item, data)
                # Test RSA verification directly; a version/digest mismatch must not mask it.
                result = subprocess.run(command+[str(changed)]+options, stdout=log, stderr=log)
                if result.returncode == 0:
                    raise AssertionError('Modified RSA signature accepted: '+target)
    print('Actual WGT signatures passed; changed author/distributor signatures rejected')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    parser.add_argument('jdk', type=Path)
    parser.add_argument('--allow-sdk-distributor', action='store_true')
    args = parser.parse_args()
    main(args.package, args.jdk, args.allow_sdk_distributor)
