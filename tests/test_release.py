import base64
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
def load(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/file)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
version=load('version','version.py');stage=load('stage','stage-web.py');wgt=load('wgt','verify-wgt.py')

class Release(unittest.TestCase):
    def test_versions(self):
        self.assertEqual(version.calculate('2.2.2',[],lambda ref:10),'2.2.12')
        tags=[('v2.2.9','a'),('v2.2.10','b'),('v1.0.0','c'),('v99.0.0-rc1','d')]
        self.assertEqual(version.calculate('2.2.2',tags,lambda ref:3 if ref=='b' else 100),'2.2.13')
        self.assertEqual(version.calculate('2.2.2',[('v2.3.0','a')],lambda ref:0),'2.3.0')
        with self.assertRaises(ValueError):version.calculate('2.2.65535',[],lambda ref:1)
    def test_stage_preserves_sources(self):
        before=(ROOT/'app/config.xml').read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            dest=stage.stage(Path(tmp)/'web','2.3.4')
            self.assertIn('version="2.3.4"',(dest/'config.xml').read_text())
            self.assertIn('id="app-version">2.3.4',(dest/'index.html').read_text())
            with self.assertRaises(ValueError):stage.stage(dest,'2.3.5')
            with self.assertRaises(ValueError):stage.stage(Path(tmp)/'bad','1.2.3;echo bad')
        self.assertEqual(before,(ROOT/'app/config.xml').read_bytes())
    def test_package_verification_rejects_unsigned_or_tampered(self):
        with tempfile.TemporaryDirectory() as tmp:
            package=Path(tmp)/'test.wgt'
            files={'config.xml':b'<widget version="2.3.4"/>','index.html':b'<html/>','js/app.js':b'','js/native-bridge.js':b'','icon.png':b'png'}
            def signature(payload):
                refs=''.join('<Reference URI="'+name+'"><DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"/><DigestValue>'+base64.b64encode(hashlib.sha256(data).digest()).decode()+'</DigestValue></Reference>' for name,data in payload.items())
                return ('<Signature xmlns="http://www.w3.org/2000/09/xmldsig#"><SignedInfo>'+refs+'</SignedInfo><SignatureValue>test fixture</SignatureValue></Signature>').encode()
            with ZipFile(package,'w') as z:
                for name,data in files.items():z.writestr(name,data)
            with self.assertRaises(ValueError):wgt.verify(package,'2.3.4')
            files['author-signature.xml']=signature(files)
            files['signature1.xml']=signature(files)
            with ZipFile(package,'w') as z:
                for name,data in files.items():z.writestr(name,data)
            wgt.verify(package,'2.3.4')
            with self.assertRaises(ValueError):wgt.verify(package,'2.3.5')
            with ZipFile(package,'a') as z:z.writestr('signature-uncovered.js',b'evil')
            with self.assertRaises(ValueError):wgt.verify(package,'2.3.4')
            with ZipFile(package,'w') as z:
                for name,data in files.items():z.writestr(name,b'tampered' if name=='index.html' else data)
            with self.assertRaises(ValueError):wgt.verify(package,'2.3.4')

if __name__=='__main__': unittest.main(verbosity=2)
