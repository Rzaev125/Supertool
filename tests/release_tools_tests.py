"""Release metadata stays consistent without network or real publication."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class ReleaseToolsTests(unittest.TestCase):
    def setUp(self):
        parent = Path.cwd() / 'test-output'
        parent.mkdir(exist_ok=True)
        self.work = tempfile.TemporaryDirectory(prefix='release-tools-', dir=parent)
        self.root = Path(self.work.name)

    def tearDown(self):
        self.work.cleanup()

    def test_verify_keeps_committed_asset_url(self):
        spec = importlib.util.spec_from_file_location('release_modules', ROOT / 'scripts/build-modules.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ROOT = self.root
        module.SPECS = [('ffmpeg', 'Fixture', '', ['ffmpeg.exe'])]
        source = self.root / 'tools/modules/ffmpeg'
        source.mkdir(parents=True)
        data = b'pinned fixture'
        (source / 'ffmpeg.exe').write_bytes(data)
        inventory = f'SWI1\n{hashlib.sha256(data).hexdigest()} {len(data)} ffmpeg.exe\n'
        assets = self.root / 'out/module-assets'
        assets.mkdir(parents=True)
        cab = assets / 'Supertool-module-ffmpeg-0.9.0.cab'
        cab.write_bytes(b'pinned CAB fixture')
        catalog = dict(id='ffmpeg', version='0.9.0', url='https://github.com/Rzaev125/Supertool/releases/download/v1.0.0/' + cab.name,
                       inventory_sha256=hashlib.sha256(inventory.encode()).hexdigest(), sha256=hashlib.sha256(cab.read_bytes()).hexdigest(), bytes=cab.stat().st_size)
        (self.root / 'resources').mkdir()
        (self.root / 'resources/module_catalog.json').write_text(json.dumps([catalog]), encoding='utf-8')
        with patch.object(sys, 'argv', ['build-modules.py', '--verify-only']):
            module.main()
        actual = json.loads((assets / 'catalog.json').read_text('utf-8'))[0]
        self.assertEqual(actual['url'], catalog['url'])
        self.assertEqual(actual['sha256'], catalog['sha256'])

    def test_publisher_accepts_reviewed_title_and_notes_without_publishing(self):
        scripts = self.root / 'scripts'
        scripts.mkdir()
        shutil.copy2(ROOT / 'scripts/publish-release.py', scripts)
        out = self.root / 'out'
        (out / 'module-assets').mkdir(parents=True)
        for name in ('Supertool-1.0.0-win-x64-setup.exe', 'Supertool-1.0.0-win-x64.zip', 'Supertool-1.0.0-source.zip', 'Supertool-1.0.0-core-win-x64-setup.exe', 'Supertool-1.0.0-core-win-x64.zip', 'module-assets/catalog.json', 'stable.txt', 'stable-signed.txt'):
            (out / name).write_text('review fixture', encoding='utf-8')
        notes = self.root / 'notes.md'
        notes.write_text('First version. Windows binaries are unsigned.', encoding='utf-8')
        result = subprocess.run([sys.executable, str(scripts / 'publish-release.py'), '--version', '1.0.0', '--title', 'First version', '--notes-file', str(notes)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('First version', result.stdout)
        self.assertTrue((out / 'Supertool-1.0.0-source.zip.sha256').exists())


if __name__ == '__main__':
    unittest.main()
