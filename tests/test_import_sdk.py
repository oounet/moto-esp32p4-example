import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
SPEC = importlib.util.spec_from_file_location('sdk_import', Path(__file__).resolve().parents[1]/'tools/import_sdk.py')
module = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(module)
class ImportSafety(unittest.TestCase):
    def test_rejects_old_abi_and_imports_matching_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            for abi, revision in [(7, 16), (8, 15), (8, 16)]:
                files = {path: b'' for path in [
                    'include/moto/sdk.h', 'include/moto/proximity.h',
                    'include/moto/network.h', 'include/moto/network_socket.h',
                    'include/moto_freertos/terminal.h', 'components/moto_prebuilt/CMakeLists.txt',
                    'lib/esp32p4-idf5.5.4/libmoto_sdk.a', 'lib/esp32p4-idf5.5.4/libmoto_freertos.a']}
                files['BUILD_INFO.json'] = json.dumps({'distribution': 'binary-only',
                    'target': 'esp32p4', 'idf_version': '5.5.4', 'freertos_abi': abi, 'package_revision': revision, 'seat_control': 1, 'capability_negotiation': 1}).encode()
                sums = ''.join(hashlib.sha256(data).hexdigest() + '  ' + path + '\n'
                               for path, data in files.items())
                archive = Path(tmp) / f'abi{abi}-revision{revision}.zip'
                with zipfile.ZipFile(archive, 'w') as z:
                    for path, data in files.items(): z.writestr('pkg/' + path, data)
                    z.writestr('pkg/SHA256SUMS', sums)
                destination = Path(tmp) / f'sdk{abi}-revision{revision}'
                if abi == 7:
                    with self.assertRaisesRegex(ValueError, 'ABI 8'):
                        module.import_package(archive, destination)
                    self.assertFalse(destination.exists())
                elif revision < 16:
                    with self.assertRaisesRegex(ValueError, 'revision 16'):
                        module.import_package(archive, destination)
                    self.assertFalse(destination.exists())
                else:
                    module.import_package(archive, destination)
                    self.assertEqual((destination / 'BUILD_INFO.json').read_bytes(), files['BUILD_INFO.json'])
                    self.assertTrue((destination / 'include/moto/proximity.h').is_file())

    def test_no_path_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp)/'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z: z.writestr('root/../../escape', 'bad')
            with self.assertRaises(ValueError): module.import_package(archive, Path(tmp)/'sdk')
            self.assertFalse((Path(tmp)/'sdk').exists())
    def test_refuses_existing_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'sdk';target.mkdir();(target/'keep').write_text('keep')
            with self.assertRaises(ValueError): module.import_package(Path(tmp)/'missing.zip', target)
            self.assertEqual((target/'keep').read_text(), 'keep')

    def test_corrupt_checksum_never_creates_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp)/'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('pkg/SHA256SUMS', '0'*64 + '  BUILD_INFO.json\n')
                z.writestr('pkg/BUILD_INFO.json', '{}')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                module.import_package(archive, Path(tmp)/'sdk')
            self.assertFalse((Path(tmp)/'sdk').exists())
