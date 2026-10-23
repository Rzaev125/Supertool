"""Offline bootstrap transactions with tiny pinned module fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import os
import tempfile
import unittest
import sys
import runpy
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/module_bootstrap.py'


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), 'Offline local-CAB bootstrap is required.')
        spec = importlib.util.spec_from_file_location('module_bootstrap', SCRIPT)
        self.api = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.api)
        self.temp = tempfile.TemporaryDirectory(prefix='bootstrap-contract-', dir=ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'resources').mkdir()
        self.assets = self.root / 'assets'
        self.assets.mkdir()
        self.data = b'pinned test engine'
        self.inventory = self.make_inventory('bin/engine.exe')
        self.cab = self.assets / 'ShiftWheel-module-pdfium-0.9.0.cab'
        self.cab.write_bytes(b'pinned cabinet fixture')
        self.spec = dict(id='pdfium', version='0.9.0', sha256=self.sha(self.cab.read_bytes()), bytes=self.cab.stat().st_size,
                         inventory_sha256=self.sha(self.inventory), count=2, unpacked=len(self.inventory)+len(self.data), required=['bin/engine.exe'], dependency='')
        self.save_catalog()

    @staticmethod
    def sha(data):
        return hashlib.sha256(data).hexdigest()

    def make_inventory(self, name):
        return f'SWI1\n{self.sha(self.data)} {len(self.data)} {name}\n'.encode()

    def save_catalog(self):
        (self.root / 'resources/module_catalog.json').write_text(json.dumps([self.spec]))

    def extract(self, spec, cab, destination):
        (destination / 'bin').mkdir()
        (destination / 'bin/engine.exe').write_bytes(self.data)
        (destination / '.sw-inventory').write_bytes(self.inventory)

    def bootstrap(self, extractor=None, **kwargs):
        return self.api.bootstrap(self.root, self.assets, extractor=extractor or self.extract, **kwargs)

    def assert_no_stage(self):
        self.assertEqual(list((self.root / 'tools').glob('.bootstrap-*')), [])

    def test_restore_then_reuse_without_unpacking_or_network(self):
        self.assertEqual(self.bootstrap()['restored'], ['pdfium'])
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        before = engine.stat().st_mtime_ns
        def unexpected(*args):
            raise AssertionError('Correct local module must be reused.')
        self.assertEqual(self.bootstrap(extractor=unexpected)['reused'], ['pdfium'])
        self.assertEqual(engine.stat().st_mtime_ns, before)
        self.assertEqual(engine.read_bytes(), self.data)
        self.assert_no_stage()

    def test_missing_or_tampered_cab_keeps_existing_tree(self):
        self.bootstrap()
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        engine.write_bytes(b'damaged existing engine')
        self.cab.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing local CAB'):
            self.bootstrap()
        self.cab.write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'CAB.*mismatch'):
            self.bootstrap()
        self.assertEqual(engine.read_bytes(), b'damaged existing engine')
        self.assert_no_stage()

    def test_partial_extraction_does_not_replace_previous_module(self):
        self.bootstrap()
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        engine.write_bytes(b'previous content')
        def partial(spec, cab, destination):
            self.extract(spec, cab, destination)
            (destination / 'bin/engine.exe').write_bytes(b'partial')
        with self.assertRaises(ValueError):
            self.bootstrap(extractor=partial)
        self.assertEqual(engine.read_bytes(), b'previous content')
        self.assert_no_stage()

    def test_unknown_files_are_preserved_and_not_silently_replaced(self):
        self.bootstrap()
        personal = self.root / 'tools/modules/pdfium/personal.txt'
        personal.write_text('preserve')
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            self.bootstrap()
        self.assertEqual(personal.read_text(), 'preserve')
        self.assert_no_stage()

    def test_inventory_paths_and_case_collisions_are_rejected(self):
        for name in ['../escape.exe', 'C:/escape.exe', 'a/CON.txt', 'a:stream', 'a\\b', 'a./b']:
            with self.subTest(name=name):
                raw = self.make_inventory(name)
                spec = dict(self.spec, inventory_sha256=self.sha(raw), unpacked=len(raw)+len(self.data))
                with self.assertRaises(ValueError):
                    self.api.parse_inventory(raw, spec)
        raw = self.inventory + self.make_inventory('BIN/ENGINE.EXE').split(b'\n', 1)[1]
        spec = dict(self.spec, count=3, inventory_sha256=self.sha(raw), unpacked=len(raw)+2*len(self.data))
        with self.assertRaises(ValueError):
            self.api.parse_inventory(raw, spec)

    def test_failed_swap_rolls_back_existing_modules(self):
        self.bootstrap()
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        engine.write_bytes(b'previous content')
        real_rename = os.rename
        def failed_swap(source, destination, *args, **kwargs):
            if Path(source).name == 'incoming':
                raise OSError('injected rename failure')
            return real_rename(source, destination, *args, **kwargs)
        with mock.patch.object(self.api.os, 'rename', side_effect=failed_swap):
            with self.assertRaises(OSError):
                self.bootstrap()
        self.assertEqual(engine.read_bytes(), b'previous content')
        self.assert_no_stage()

    def test_verify_only_does_not_repair_changed_files(self):
        self.bootstrap()
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        engine.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            self.bootstrap(verify_only=True)
        self.assertEqual(engine.read_bytes(), b'changed')

    def test_interrupt_after_backup_rename_rolls_back(self):
        self.bootstrap()
        engine = self.root / 'tools/modules/pdfium/bin/engine.exe'
        engine.write_bytes(b'previous content')
        real_rename = os.rename
        def interrupted(source, destination, *args, **kwargs):
            answer = real_rename(source, destination, *args, **kwargs)
            if Path(destination).name == 'previous':
                raise KeyboardInterrupt('interrupted after backup was created')
            return answer
        with mock.patch.object(self.api.os, 'rename', side_effect=interrupted):
            with self.assertRaises(KeyboardInterrupt):
                self.bootstrap()
        self.assertTrue(engine.is_file(), 'interruption must not delete the previous module tree')
        self.assertEqual(engine.read_bytes(), b'previous content')

    def test_module_packaging_verification_never_downloads_missing_cab(self):
        self.bootstrap()
        self.spec['url'] = 'https://example.invalid/removed.cab'
        self.save_catalog()
        spec = importlib.util.spec_from_file_location('build_modules_test', ROOT / 'scripts/build-modules.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = self.root
        module.SPECS = [('pdfium', 'Fixture', '', ['bin/engine.exe'])]
        with mock.patch.object(sys, 'argv', ['build-modules.py', '--verify-only']), mock.patch('urllib.request.urlopen', side_effect=AssertionError('Network must not be used')):
            with self.assertRaisesRegex(ValueError, 'Missing local CAB'):
                module.main()

    def test_native_acceptance_failure_keeps_its_fixture_under_runner_root(self):
        artifacts = self.root / 'runner-artifacts'
        artifacts.mkdir()
        with mock.patch.dict(os.environ, SHIFTWHEEL_TEST_ARTIFACT_ROOT=str(artifacts)), mock.patch.dict(sys.modules, module_bootstrap=self.api), mock.patch.object(sys, 'argv', ['bootstrap_native_tests.py']), mock.patch.object(self.api, 'bootstrap', side_effect=ValueError('injected native acceptance failure')):
            with self.assertRaisesRegex(ValueError, 'injected native acceptance failure'):
                runpy.run_path(str(ROOT / 'tests/bootstrap_native_tests.py'), run_name='__main__')
        self.assertTrue(list(artifacts.glob('bootstrap-acceptance-*')), 'failed native acceptance fixture must be retained inside runner workspace')


if __name__ == '__main__':
    unittest.main()
