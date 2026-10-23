"""Exercise real CTest orchestration and ownership checks on tiny test trees."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/local-check.py'
CTEST = shutil.which('ctest') or r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'


class LocalCheckTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), 'A maintained local check command is required.')
        spec = importlib.util.spec_from_file_location('local_check', SCRIPT)
        self.api = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.api)
        self.temp = tempfile.TemporaryDirectory(prefix='check-contract-', dir=ROOT / 'build')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'build').mkdir()
        fixture = self.root / 'fixture.py'
        fixture.write_text("from pathlib import Path\nPath('created-fixture.txt').write_text('fixture')\nprint('fixture passed')\n")
        self.build = self.root / 'build'
        self.testfile = self.build / 'CTestTestfile.cmake'
        self.testfile.write_text('add_test(conversion_smoke [=[' + sys.executable + ']=] [=[' + str(fixture) + ']=])\n')

    def run_checks(self, suite='smoke'):
        return self.api.run_checks(self.root, self.build, suite, 'Release', Path(CTEST))

    def test_two_successful_runs_leave_only_bounded_reports(self):
        for _ in range(2):
            self.assertEqual(self.run_checks(), 0)
            self.assertEqual(list((self.build / 'local-check/runs').iterdir()), [])
        report = json.loads((self.build / 'local-check/report.json').read_text())
        self.assertEqual(report['status'], 'passed')
        self.assertEqual(report['executed'], ['conversion_smoke'])
        self.assertTrue(report['fixtures_removed'])

    def test_failure_keeps_diagnostics_then_explicit_cleanup_removes_only_owned_run(self):
        (self.root / 'fixture.py').write_text("from pathlib import Path\nPath('diagnostic-input.txt').write_text('keep for investigation')\nraise SystemExit(7)\n")
        self.assertNotEqual(self.run_checks(), 0)
        report = json.loads((self.build / 'local-check/report.json').read_text())
        workspace = Path(report['workspace'])
        self.assertTrue((workspace / 'diagnostic-input.txt').is_file())
        self.assertTrue((self.build / 'local-check/check.log').is_file())
        sentinel = self.root / 'personal.txt'
        sentinel.write_text('unchanged')
        self.assertEqual(self.api.cleanup_runs(self.root, apply=False), [str(workspace)])
        self.assertTrue(workspace.exists())
        self.assertEqual(self.api.cleanup_runs(self.root, apply=True), [str(workspace)])
        self.assertFalse(workspace.exists())
        self.assertEqual(sentinel.read_text(), 'unchanged')

    def test_cleanup_rejects_unowned_and_outside_paths(self):
        self.run_checks()
        unowned = self.build / 'local-check/runs' / ('a' * 32)
        unowned.mkdir()
        (unowned / 'personal.txt').write_text('user data')
        self.assertEqual(self.api.cleanup_runs(self.root, apply=True), [])
        with self.assertRaises(ValueError):
            self.api.remove_run(self.root, unowned)
        with self.assertRaises(ValueError):
            self.api.remove_run(self.root, self.root)
        self.assertTrue((unowned / 'personal.txt').is_file())

    def test_cleanup_rejects_junction_and_does_not_delete_target(self):
        (self.root / 'fixture.py').write_text('raise SystemExit(1)\n')
        self.run_checks()
        report = json.loads((self.build / 'local-check/report.json').read_text())
        workspace = Path(report['workspace'])
        link = workspace / 'link'
        target = self.root / 'protected'
        target.mkdir()
        (target / 'sentinel').write_text('protected')
        command = "New-Item -ItemType Junction -Path $args[0] -Target $args[1] | Out-Null"
        # Pass paths as PowerShell arguments, never shell-interpolated strings.
        script = self.root / 'link.ps1'
        script.write_text(command)
        subprocess.run(['powershell', '-NoProfile', '-File', str(script), str(link), str(target)], check=True, capture_output=True)
        try:
            with self.assertRaises(ValueError):
                self.api.remove_run(self.root, workspace)
            self.assertEqual((target / 'sentinel').read_text(), 'protected')
        finally:
            link.rmdir()  # Remove the junction itself, without traversing it.
        self.api.remove_run(self.root, workspace)

    def test_missing_suite_is_reported_as_failure_not_success(self):
        self.assertNotEqual(self.run_checks('conversions'), 0)
        report = json.loads((self.build / 'local-check/report.json').read_text())
        self.assertEqual(report['status'], 'failed')
        self.assertIn('missing', report['error'].lower())

    def test_outside_path_after_normalization_is_rejected(self):
        with self.assertRaises(ValueError):
            self.api.assert_local(self.root, self.root / 'build/../../outside')

    def test_disabled_smoke_is_not_reported_as_a_successful_check(self):
        with self.testfile.open('a') as stream:
            stream.write('set_tests_properties(conversion_smoke PROPERTIES DISABLED TRUE)\n')
        result = self.run_checks()
        self.assertNotEqual(result, 0, (self.build / 'local-check/results.xml').read_text())

    def test_active_workspace_is_excluded_from_cleanup(self):
        (self.root / 'fixture.py').write_text('raise SystemExit(1)\n')
        self.run_checks()
        report = json.loads((self.build / 'local-check/report.json').read_text())
        workspace = Path(report['workspace'])
        with self.api.lock_run(workspace):
            self.assertEqual(self.api.cleanup_runs(self.root, apply=True), [])
            self.assertTrue(workspace.exists())
        self.api.remove_run(self.root, workspace)


if __name__ == '__main__':
    unittest.main()
