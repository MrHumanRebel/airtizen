#!/usr/bin/env python3
"""Give the temporary CLI profile explicit passwords without a desktop keyring."""
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def prepare(path, name):
    passwords = {'0': os.environ['TIZEN_CERT_PASSWORD'],
                 '1': os.environ['TIZEN_DISTRIBUTOR_PASSWORD']}
    if not all(passwords.values()):
        raise ValueError('Both signing passwords are required')
    sdk = Path(os.environ['TIZEN_STUDIO_ROOT'])
    classpath = str(sdk/'tools/ide/lib-ncli/*')
    with tempfile.TemporaryDirectory(prefix='password-codec-', dir=path.parent) as directory:
        subprocess.run([str(sdk/'jdk/bin/javac'), '-cp', classpath, '-d', directory,
                        str(Path(__file__).with_name('EncodeSigningPasswords.java'))], check=True)
        encoded = subprocess.check_output([str(sdk/'jdk/bin/java'), '-cp',
                                           directory+os.pathsep+classpath,
                                           'EncodeSigningPasswords'], text=True).splitlines()
    if len(encoded) != 2 or not all(encoded):
        raise ValueError('SDK did not encode both signing passwords')
    passwords = dict(zip(('0', '1'), encoded))
    tree = ET.parse(path)
    profiles = [profile for profile in tree.getroot() if profile.get('name') == name]
    if len(profiles) != 1:
        raise ValueError('Signing profile creation failed')
    for role, password in passwords.items():
        items = [item for item in profiles[0] if item.get('distributor') == role]
        if len(items) != 1 or not Path(items[0].get('key', '')).is_file():
            raise ValueError('Missing signing key for role '+role)
        # CLI-generated .pwd references depend on desktop secure storage. The
        # SDK supports encrypted inline values through CipherUtil instead.
        # ElementTree handles XML-special characters; never print this document.
        items[0].set('password', password)
    os.chmod(path, 0o600)
    tree.write(path, encoding='utf-8', xml_declaration=True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: prepare-signing-profile.py PRIVATE_PROFILES_XML NAME')
    prepare(Path(sys.argv[1]), sys.argv[2])
