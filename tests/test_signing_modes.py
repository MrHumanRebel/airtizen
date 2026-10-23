#!/usr/bin/env python3
"""Exercise real SDK signing with no secrets, configured P12s, and rejected identities."""
import base64
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class Signing(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sdk = Path(os.environ['TIZEN_STUDIO_ROOT']).resolve()
        cls.public = ROOT/'certificates/community-author.p12'
        cls.distributor = cls.sdk/'tools/certificate-generator/certificates/distributor/tizen-distributor-signer.p12'

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='airtizen-signing-test-')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.stage = self.directory/'stage'
        self.output = self.directory/'output'
        subprocess.run([sys.executable, str(ROOT/'scripts/stage-web.py'), str(self.stage), '9.8.7'], check=True, stdout=subprocess.DEVNULL)
        self.env = {k: v for k, v in os.environ.items() if k not in (
            'TIZEN_CERT_P12_BASE64', 'TIZEN_CERT_PASSWORD',
            'TIZEN_DISTRIBUTOR_P12_BASE64', 'TIZEN_DISTRIBUTOR_PASSWORD')}
        self.env.update(GITHUB_WORKSPACE=str(ROOT), RUNNER_TEMP=str(self.directory))

    def build(self):
        return subprocess.run(['bash', str(ROOT/'scripts/build-signed-wgt.sh'),
                               str(self.stage), str(self.output)], env=self.env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def test_no_secrets_public_package(self):
        # A complete widget can exceed XMLDSig's document-oriented default of
        # 30 references. Sign actual additional files, not fabricated XML.
        for i in range(35):
            (self.stage/('reference-%02d.txt'%i)).write_text('Signed payload %d\n'%i)
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotIn('Author password:', result.stdout)
        self.assertNotIn('Distributor1 password:', result.stdout)
        self.assertEqual((self.output/'SIGNING-MODE.txt').read_text().splitlines(),
                         ['public-community-author', 'public-sdk-distributor'])
        subprocess.run([sys.executable, str(ROOT/'tests/test_wgt_crypto.py'),
                        str(self.output/'AirTizen.wgt'), str(self.sdk/'jdk'),
                        '--allow-sdk-distributor'], check=True)

    def test_profile_signs_without_terminal_or_password_storage(self):
        tizen = str(self.sdk/'tools/ide/bin/tizen')
        profile = self.directory/'profiles.xml'
        profiles = ET.Element('profiles', active='HeadlessRegression', version='3.1')
        entry = ET.SubElement(profiles, 'profile', name='HeadlessRegression')
        for role, key in (('0', self.public), ('1', self.distributor)):
            ET.SubElement(entry, 'profileitem', distributor=role, key=str(key),
                          password=str(self.directory/('missing-'+role+'.pwd')), ca='', rootca='')
        ET.ElementTree(profiles).write(profile, encoding='utf-8', xml_declaration=True)
        self.env.update(TIZEN_CERT_PASSWORD='1234',
                        TIZEN_DISTRIBUTOR_PASSWORD='tizenpkcs12passfordsigner')
        config = subprocess.check_output([tizen, 'cli-config', '-l'], env=self.env, text=True)
        original = next((line.partition('=')[2].strip() for line in config.splitlines()
                         if line.partition('=')[0].strip() == 'profiles.path'),
                        str(self.sdk)+'-data/profile/profiles.xml')
        try:
            subprocess.run([tizen, 'cli-config', 'profiles.path='+str(profile)], env=self.env,
                           check=True, stdout=subprocess.DEVNULL)
            subprocess.run([tizen, 'build-web', '--', str(self.stage)], env=self.env,
                           check=True, stdout=subprocess.DEVNULL)
            command = [tizen, 'package', '-t', 'wgt', '-s', 'HeadlessRegression']
            self.output.mkdir()
            before = subprocess.run(command+['-o', str(self.output), '--',
                                             str(self.stage/'.buildResult')], env=self.env,
                                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=30, text=True)
            self.assertNotEqual(before.returncode, 0, 'Missing .pwd storage unexpectedly worked')
            self.assertFalse(list(self.output.glob('*.wgt')))
            subprocess.run([sys.executable, str(ROOT/'scripts/prepare-signing-profile.py'),
                            str(profile), 'HeadlessRegression'], env=self.env, check=True)
            self.assertEqual(profile.stat().st_mode & 0o777, 0o600)
            after = subprocess.run(command+['-o', str(self.output), '--',
                                            str(self.stage/'.buildResult')], env=self.env,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=30, text=True)
            self.assertEqual(after.returncode, 0, after.stdout)
            packages = list(self.output.glob('*.wgt'))
            self.assertEqual(len(packages), 1)
            subprocess.run([sys.executable, str(ROOT/'scripts/verify-wgt.py'),
                            str(packages[0]), '9.8.7', '--jdk', str(self.sdk/'jdk'),
                            '--allow-sdk-distributor'], env=self.env, check=True)
        finally:
            subprocess.run([tizen, 'cli-config', 'profiles.path='+original], env=self.env,
                           check=True, stdout=subprocess.DEVNULL)

    def test_configured_certificates_keep_strict_policy(self):
        encoded = base64.b64encode(self.public.read_bytes()).decode()
        # Integrity-policy fixture only: both roles use this strong public test key.
        # This fixture does not establish distributor trust on a TV.
        self.env.update(TIZEN_CERT_P12_BASE64=encoded, TIZEN_CERT_PASSWORD='1234',
                        TIZEN_DISTRIBUTOR_P12_BASE64=encoded, TIZEN_DISTRIBUTOR_PASSWORD='1234')
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual((self.output/'SIGNING-MODE.txt').read_text().splitlines(),
                         ['configured-author', 'configured-distributor'])
        subprocess.run([sys.executable, str(ROOT/'tests/test_wgt_crypto.py'),
                        str(self.output/'AirTizen.wgt'), str(self.sdk/'jdk')], check=True)

    def test_incomplete_configured_identity_does_not_fallback(self):
        self.env['TIZEN_CERT_P12_BASE64'] = base64.b64encode(self.public.read_bytes()).decode()
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('configured author P12 requires its password', result.stdout)
        self.assertFalse((self.output/'AirTizen.wgt').exists())

    def test_sdk_exception_cannot_be_used_for_author(self):
        self.env.update(TIZEN_CERT_P12_BASE64=base64.b64encode(self.distributor.read_bytes()).decode(),
                        TIZEN_CERT_PASSWORD='tizenpkcs12passfordsigner')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RSA key below policy minimum', result.stdout)
        self.assertFalse((self.output/'SIGNING-MODE.txt').exists())

    def test_sdk_exception_does_not_allow_other_weak_distributors(self):
        weak = self.directory/'unrecognized.p12'
        password = 'regression-only'
        subprocess.run([str(self.sdk/'jdk/bin/keytool'), '-genkeypair',
                        '-alias', 'regression', '-keyalg', 'RSA', '-keysize', '1024',
                        '-sigalg', 'SHA256withRSA', '-validity', '1',
                        '-dname', 'CN=Unrecognized Weak Test Key', '-storetype', 'PKCS12',
                        '-keystore', str(weak), '-storepass', password,
                        '-keypass', password, '-noprompt'], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.env.update(TIZEN_DISTRIBUTOR_P12_BASE64=base64.b64encode(weak.read_bytes()).decode(),
                        TIZEN_DISTRIBUTOR_PASSWORD=password)
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RSA key below policy minimum', result.stdout)
        # The explicit SDK exception must still reject this unrelated weak key.
        result = subprocess.run([sys.executable, str(ROOT/'scripts/verify-wgt.py'),
                                 str(self.output/'AirTizen.wgt'), '9.8.7',
                                 '--jdk', str(self.sdk/'jdk'), '--allow-sdk-distributor'],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RSA key below policy minimum', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
