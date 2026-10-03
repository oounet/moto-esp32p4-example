import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
SPEC = importlib.util.spec_from_file_location('sdk_import', Path(__file__).resolve().parents[1]/'tools/import_sdk.py')
module = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(module)
class ImportSafety(unittest.TestCase):
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
