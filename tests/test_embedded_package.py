"""Exercise the real ARM receiver payload and reject incomplete/external bundles."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


embedded = load('embedded-receiver')
staging = load('stage-web')


class EmbeddedPackage(unittest.TestCase):
    def test_real_arm_payload_and_missing_files(self):
        self.assertIsNotNone(MODULE, 'Pass the actual SDK-validated ARM receiver path')
        with tempfile.TemporaryDirectory() as temp:
            destination = staging.stage(Path(temp) / 'web', '2.3.4', MODULE)
            files = {str(p.relative_to(destination)): p.read_bytes()
                     for p in destination.rglob('*') if p.is_file()}
            embedded.check_embedded(files.__getitem__)
            self.assertEqual(files['receiver/receiver_arm.nexe'], MODULE.read_bytes())
            for required in ('receiver/receiver.nmf', 'receiver/receiver_arm.nexe', 'js/embedded-receiver.js'):
                missing = dict(files)
                del missing[required]
                with self.assertRaises((KeyError, ValueError)):
                    embedded.check_embedded(missing.__getitem__)
            for url in ('https://remote/receiver.nexe', '../outside.nexe', '/tmp/module', 'bad\\file'):
                changed = dict(files)
                import json
                changed['receiver/receiver.nmf'] = json.dumps({'program': {'arm': {'url': url}}}).encode()
                with self.assertRaises(ValueError):
                    embedded.check_embedded(changed.__getitem__)
            for payload in (b'', b'not a native module', Path('/bin/true').read_bytes(), MODULE.read_bytes()[:100]):
                with self.assertRaises(ValueError):
                    embedded.check_module(payload)
            self.assertTrue((destination / 'licenses/NaCl-NOTICE.txt').stat().st_size)
            self.assertTrue((destination / 'licenses/FFmpeg-COPYING.LGPLv2.1').stat().st_size)


if __name__ == '__main__':
    unittest.main(verbosity=2)
