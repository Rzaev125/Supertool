"""Local CTest runner. Only marked, uniquely allocated test workspaces are cleaned."""
import argparse
from contextlib import contextmanager
import ctypes
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
CONVERSIONS = ['images', 'worker_smoke', 'conversion_pdf', 'conversion_office', 'conversion_media',
               'conversion_avif', 'conversion_formats', 'conversion_presentations', 'conversion_ocr', 'conversion_heic']
MARKER = '.local-check-owner.json'


def assert_local(root, path):
    root, path = Path(os.path.abspath(root)), Path(os.path.abspath(path))
    if not path.is_relative_to(root):
        raise ValueError('Path is outside the project: ' + str(path))
    for item in [root, *[root.joinpath(*path.relative_to(root).parts[:n]) for n in range(1, len(path.relative_to(root).parts) + 1)]]:
        try:
            attrs = item.lstat().st_file_attributes
        except FileNotFoundError:
            continue
        if attrs & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Reparse point is not allowed: ' + str(item))
    return path


def base_directory(root):
    return assert_local(root, Path(root) / 'build/local-check')


@contextmanager
def lock_run(workspace):
    lock = workspace / '.run-lock'
    if lock.exists() and lock.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError('Reparse point in workspace lock.')
    with lock.open('a+b') as handle:
        if not handle.tell():
            handle.write(b'1')
            handle.flush()
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            yield
        finally:
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)


def owned_run(root, workspace):
    base = base_directory(root)
    workspace = assert_local(root, workspace)
    if workspace.parent != base / 'runs' or not re.fullmatch('[a-f0-9]{32}', workspace.name):
        raise ValueError('Not a local-check workspace.')
    marker = assert_local(root, workspace / MARKER)
    try:
        owner = json.loads(marker.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise ValueError('Missing or invalid workspace ownership marker.') from error
    if owner != dict(kind='ShiftWheelLocalCheck', schema=1, root=str(Path(root).absolute())):
        raise ValueError('Workspace ownership does not match this project.')
    return workspace


def remove_run(root, workspace):
    workspace = owned_run(root, workspace)
    # Check without following links, including files and the lock, before deleting.
    for directory, folders, files in os.walk(workspace, followlinks=False):
        for name in folders + files:
            assert_local(root, Path(directory) / name)
    with lock_run(workspace):
        pass  # A running check holds this lock. Do not remove a live workspace.
    shutil.rmtree(workspace)


def cleanup_runs(root, apply=False):
    directory = base_directory(root) / 'runs'
    if not directory.exists():
        return []
    candidates = []
    for workspace in sorted(directory.iterdir()):
        try:
            owned_run(root, workspace)
            with lock_run(workspace):
                pass
        except (ValueError, OSError):
            continue  # Unmarked directories and active checks are not ours to clean.
        if apply:
            remove_run(root, workspace)
        candidates.append(str(workspace))
    return candidates


def find_ctest(build):
    found = shutil.which('ctest')
    cache = build / 'CMakeCache.txt'
    if not found and cache.exists():
        match = re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache.read_text(encoding='utf-8'), re.MULTILINE)
        if match:
            candidate = Path(match[1]).with_name('ctest.exe')
            if candidate.is_file():
                found = str(candidate)
    if not found:
        raise ValueError('CTest not found. Configure the project with CMake first, or pass --ctest.')
    return Path(found)


def tray_running():
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenMutexW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
    kernel.OpenMutexW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenMutexW(0x00100000, False, 'Local\\ShiftWheel.Tray')
    if handle:
        kernel.CloseHandle(handle)
        return True
    return ctypes.get_last_error() == 5  # Access denied also means it exists.


def bracket(value):
    value = str(value)
    fence = '='
    while ']' + fence + ']' in value:
        fence += '='
    return '[' + fence + '[' + value + ']' + fence + ']'


def run_checks(root, build, suite, configuration, ctest):
    root, build = Path(root).absolute(), Path(build).absolute()
    assert_local(root, build)
    base = base_directory(root)
    runs = assert_local(root, base / 'runs')
    runs.mkdir(parents=True, exist_ok=True)
    workspace = runs / uuid.uuid4().hex
    workspace.mkdir()
    report = dict(status='failed', suite=suite, executed=[], skipped=[], workspace=str(workspace), fixtures_removed=False)
    log = assert_local(root, base / 'check.log')
    junit = assert_local(root, base / 'results.xml')
    report_path = assert_local(root, base / 'report.json')
    log.write_text('Preparing local checks.\n', encoding='utf-8')
    if junit.exists():
        junit.unlink()
    with lock_run(workspace):
        (workspace / MARKER).write_text(json.dumps(dict(kind='ShiftWheelLocalCheck', schema=1, root=str(root))), encoding='utf-8')
        try:
            discovery = subprocess.run([str(ctest), '--test-dir', str(build), '-C', configuration, '--show-only=json-v1'],
                                       capture_output=True, timeout=30, check=True)
            tests = json.loads(discovery.stdout)['tests']
            wanted = ['conversion_smoke'] if suite == 'smoke' else CONVERSIONS if suite == 'conversions' else [t['name'] for t in tests]
            missing = set(wanted) - {t['name'] for t in tests}
            if missing:
                raise ValueError('Missing required tests: ' + ', '.join(sorted(missing)))
            if not wanted:
                raise ValueError('Missing tests in configured build.')
            tests = [t for t in tests if t['name'] in wanted]
            if suite == 'full' and tray_running():
                tests = [t for t in tests if t['name'] != 'application_smoke']
                report['skipped'].append(dict(name='application_smoke', reason='Running tray instance is preserved; test needs a separate session.'))
            lines = []
            for test in tests:
                if not test.get('command') or test['command'][0] == 'NOT_AVAILABLE':
                    raise ValueError('Missing compiled test: ' + test['name'])
                lines.append('add_test(' + ' '.join(bracket(x) for x in [test['name'], *test['command']]) + ')')
                properties = {p['name']: p['value'] for p in test.get('properties', [])}
                lines.append('set_tests_properties(' + bracket(test['name']) + ' PROPERTIES WORKING_DIRECTORY ' + bracket(workspace)
                             + ' TIMEOUT ' + str(properties.get('TIMEOUT', 300)) + ')')
                for prop in ['ENVIRONMENT', 'SKIP_RETURN_CODE', 'DISABLED', 'WILL_FAIL', 'PASS_REGULAR_EXPRESSION', 'FAIL_REGULAR_EXPRESSION']:
                    if prop in properties:
                        value = properties[prop]
                        if isinstance(value, list):
                            value = ';'.join(map(str, value))
                        if isinstance(value, bool):
                            value = 'TRUE' if value else 'FALSE'
                        lines.append('set_tests_properties(' + bracket(test['name']) + ' PROPERTIES ' + prop + ' ' + bracket(value) + ')')
            (workspace / 'CTestTestfile.cmake').write_text('\n'.join(lines), encoding='utf-8')
            for name in ['test-output', 'temp']:
                (workspace / name).mkdir()
            env = os.environ.copy()
            env['TEMP'] = env['TMP'] = str(workspace / 'temp')
            env['SHIFTWHEEL_TEST_ARTIFACT_ROOT'] = str(workspace)
            print('Local check: ' + suite + ' (' + str(len(tests)) + ' test groups)', flush=True)
            with log.open('w', encoding='utf-8') as output:
                result = subprocess.run([str(ctest), '--test-dir', str(workspace), '-C', configuration, '--verbose', '--output-junit', str(junit)],
                                        stdout=output, stderr=subprocess.STDOUT, env=env)
            report['exit_code'] = result.returncode
            cases = ET.parse(junit).getroot().findall('.//testcase')
            if {c.attrib['name'] for c in cases} != {t['name'] for t in tests}:
                raise ValueError('Missing test results in CTest report.')
            skipped = lambda c: c.find('skipped') is not None or c.attrib.get('status') in ('disabled', 'notrun')
            report['executed'] = [c.attrib['name'] for c in cases if not skipped(c)]
            report['skipped'] += [dict(name=c.attrib['name'], reason='Skipped or disabled by CTest.') for c in cases if skipped(c)]
            if not report['executed']:
                raise ValueError('All required tests were skipped; nothing was checked.')
            report['status'] = 'passed' if result.returncode == 0 else 'failed'
        except (OSError, ValueError, ET.ParseError, subprocess.SubprocessError) as error:
            report.update(error=str(error), exit_code=1)
            with log.open('a', encoding='utf-8') as output:
                output.write('\nLocal check error: ' + str(error) + '\n')
    if report['status'] == 'passed':
        remove_run(root, workspace)
        report['fixtures_removed'] = True
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(report['status'].upper() + ': ' + str(report_path), flush=True)
    if report['status'] != 'passed':
        print('Diagnostic workspace retained: ' + str(workspace), flush=True)
    return report['exit_code']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=['smoke', 'conversions', 'full'], default='smoke')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--configuration', default='Release')
    parser.add_argument('--ctest', type=Path)
    parser.add_argument('--cleanup', action='store_true', help='Preview marked, inactive test workspaces.')
    parser.add_argument('--apply', action='store_true', help='Delete the listed owned workspaces; requires --cleanup.')
    args = parser.parse_args()
    if args.apply and not args.cleanup:
        parser.error('--apply requires --cleanup')
    try:
        if args.cleanup:
            for path in cleanup_runs(ROOT, args.apply):
                print(('REMOVED ' if args.apply else 'WOULD REMOVE ') + path)
            return 0
        return run_checks(ROOT, args.build_dir, args.suite, args.configuration, args.ctest or find_ctest(args.build_dir))
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
