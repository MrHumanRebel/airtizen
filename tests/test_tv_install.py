"""Regression for the public-author trust chain and 2017 TV manifest inputs."""
import importlib.util
from pathlib import Path
import tempfile
import os
import subprocess
import unittest
import xml.etree.ElementTree as ET
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.serialization import pkcs12
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('public_author', ROOT/'scripts/check-public-author.py')
author = importlib.util.module_from_spec(spec)
spec.loader.exec_module(author)


class TVInstall(unittest.TestCase):
    def test_default_author_has_sdk_chain_and_original_key(self):
        certificate = author.check(ROOT/'certificates/community-author.p12')
        self.assertNotEqual(certificate.issuer, certificate.subject)

    def test_old_self_signed_shape_is_rejected(self):
        key, leaf, _ = pkcs12.load_key_and_certificates((ROOT/'certificates/community-author.p12').read_bytes(), b'1234')
        old = (x509.CertificateBuilder().subject_name(leaf.subject).issuer_name(leaf.subject)
               .public_key(key.public_key()).serial_number(1)
               .not_valid_before(leaf.not_valid_before_utc).not_valid_after(leaf.not_valid_after_utc)
               .add_extension(x509.BasicConstraints(ca=False,path_length=None),critical=True).sign(key,hashes.SHA256()))
        data = pkcs12.serialize_key_and_certificates(b'old-public-author',key,old,None,serialization.BestAvailableEncryption(b'1234'))
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'old.p12';path.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'SDK developer CA chain'): author.check(path)

    def test_install_helper_rejects_platform_failure_even_when_cli_returns_zero(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);package=directory/'AirTizen.wgt';package.write_bytes(b'fixture')
            (directory/'sdb').write_text('#!/bin/sh\nexit 0\n')
            (directory/'tizen').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$ARG_FILE"\nprintf "%s\\n" "$PLATFORM_LOG"\nexit 0\n')
            for name in ('sdb','tizen'): (directory/name).chmod(0o755)
            env=dict(os.environ,PATH=str(directory)+os.pathsep+os.environ['PATH'],ARG_FILE=str(directory/'args'))
            command=['bash',str(ROOT/'scripts/install-to-tv.sh'),'192.0.2.10',str(package),'UE65MU6100']
            for log,expected in [('install failed[118]\ncmd_ret:0',1),('Tizen application is successfully installed.',0),('cmd_ret:0',1)]:
                result=subprocess.run(command,env=dict(env,PLATFORM_LOG=log),capture_output=True,text=True)
                self.assertEqual(result.returncode,expected,result.stdout+result.stderr)
            self.assertEqual((directory/'args').read_text().splitlines(),['install','-n','AirTizen.wgt','-t','UE65MU6100','--',str(directory)])
            result=subprocess.run(command[:-1],env=dict(env,PLATFORM_LOG='Tizen application is successfully installed.'),capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual((directory/'args').read_text().splitlines(),['install','-n','AirTizen.wgt','-s','192.0.2.10:26101','--',str(directory)])

    def test_tv_manifest_preserves_app_identity_and_avoids_resolution_filter(self):
        widget = ET.parse(ROOT/'app/config.xml').getroot()
        ns = {'w':'http://www.w3.org/ns/widgets','t':'http://tizen.org/ns/widgets'}
        self.assertIn(urlparse(widget.get('id')).scheme,('http','https'))
        app = widget.find('t:application',ns)
        self.assertEqual(app.get('id'),'AirTizen01.AirTizen')
        self.assertEqual(app.get('package'),'AirTizen01')
        self.assertEqual(app.get('required_version'),'3.0')
        self.assertEqual([f.get('name') for f in widget.findall('w:feature',ns)],['http://tizen.org/feature/screen.size.all'])
        self.assertEqual(widget.find('t:setting',ns).get('background-support'),'disable')
        self.assertEqual(widget.find('t:profile',ns).get('name'),'tv-samsung')


if __name__ == '__main__': unittest.main(verbosity=2)
