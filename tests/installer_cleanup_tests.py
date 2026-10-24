"""Run real installer scripts with tiny payloads; never install or launch them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=Path,default=ROOT/'build/Release/Supertool.exe')
EXE=parser.parse_args().exe.resolve()
SHELL = os.environ.get('SHIFTWHEEL_CLEANUP_TEST_SHELL', 'powershell')


def run(script, *args):
    return subprocess.run(
        [SHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), *map(str, args)],
        capture_output=True, timeout=90, creationflags=subprocess.CREATE_NO_WINDOW,
    )


def clean_workspaces(root):
    leftovers = list((root / 'tools').glob('installer-tool-stage-*'))
    leftovers += list((root / 'build').glob('installer-build-*'))
    assert not leftovers, 'temporary installer directories remain: ' + ', '.join(p.name for p in leftovers)


def check_result(result, success, marker=b''):
    output = result.stdout + result.stderr
    assert (result.returncode == 0) == success, output.decode(errors='replace')
    assert marker.lower() in output.lower(), output.decode(errors='replace')


with tempfile.TemporaryDirectory(prefix='installer-cleanup-test-', dir=ROOT / 'build') as temporary:
    root = Path(temporary)
    for folder in ['scripts', 'build/Release', 'out', 'tools/installer-downloads', 'resources/branding']:
        (root / folder).mkdir(parents=True)
    for name in ['build-installer.ps1', 'fetch-installer-tools.ps1', 'installer-tools.json', 'installer-workspace.ps1']:
        if (ROOT / 'scripts' / name).exists():
            shutil.copyfile(ROOT / 'scripts' / name, root / 'scripts' / name)
    shutil.copytree(ROOT / 'installer', root / 'installer')
    shutil.copyfile(ROOT / 'resources/branding/supertool.ico', root / 'resources/branding/supertool.ico')
    for entry in json.loads((ROOT / 'scripts/installer-tools.json').read_text()):
        source = ROOT / 'tools/installer-downloads' / entry['filename']
        if source.exists():
            assert hashlib.sha256(source.read_bytes()).hexdigest() == entry['sha256'], 'Invalid cached installer tools.'
            shutil.copyfile(source, root / 'tools/installer-downloads' / entry['filename'])
    # A verified Core payload exercises normal output copying without full modules.
    payload = root / 'payload'
    payload.mkdir()
    shutil.copyfile(EXE, payload / 'Supertool.exe')
    version = re.search(r'project\(Supertool VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    metadata = dict(version=version, distribution='core', executable_sha256=hashlib.sha256((payload / 'Supertool.exe').read_bytes()).hexdigest())
    (payload / 'release.json').write_text(json.dumps(metadata))
    script = root / 'scripts/build-installer.ps1'
    args = ['-Version', version, '-Distribution', 'core', '-PayloadDirectory', payload]
    first = run(script, *args)
    check_result(first, True)
    setup = root / f'out/Supertool-{version}-core-win-x64-setup.exe'
    assert setup.is_file() and setup.read_bytes()[:2] == b'MZ'
    assert Path(str(setup) + '.sha256').is_file()
    assert Path(str(setup) + '.payload.json').is_file()
    clean_workspaces(root)
    check_result(run(script, *args), True)
    clean_workspaces(root)
    print('PASS: repeated successful builds retain installers and remove temporary trees', flush=True)

    isolated = root / 'build/installer-tests/isolated'
    check_result(run(script, *args, '-TestRoot', isolated), True)
    assert list(isolated.rglob('*-ISOLATED-TEST.exe')), 'isolated installer must survive cleanup'
    clean_workspaces(root)
    original_setup = hashlib.sha256(setup.read_bytes()).hexdigest()
    print('PASS: isolated installer survives cleanup', flush=True)

    full = root / 'full-payload'
    (full / 'modules/engine').mkdir(parents=True)
    (full / 'Supertool.exe').write_bytes(b'fixture, never executed')
    (full / 'modules/engine/runtime.dll').write_bytes(b'fixture runtime')
    check_result(run(script, '-Version', '0.4.0', '-PayloadDirectory', full, '-TestRoot', isolated), True)
    clean_workspaces(root)
    assert list(isolated.rglob('*0.4.0*-ISOLATED-TEST.exe'))
    print('PASS: full isolated fixture remains usable by lifecycle tests', flush=True)

    # A callback error must not escape ownership of the unpacked compiler.
    callback = root / 'scripts/callback-error.ps1'
    callback.write_text("& (Join-Path $PSScriptRoot 'fetch-installer-tools.ps1') -UseCompiler { throw 'deliberate callback failure' }\n")
    check_result(run(callback), False, b'deliberate callback failure')
    clean_workspaces(root)
    print('PASS: compiler callback failure cleans unpacked tools', flush=True)

    safety = root / 'scripts/cleanup-safety.ps1'
    safety.write_text(r'''
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'installer-workspace.ps1')
$root = Split-Path $PSScriptRoot -Parent
$protected = Join-Path $root 'payload'
$rejected = $false
try { Remove-InstallerWorkspace $root $protected } catch { $rejected = $_ -match 'non-temporary' }
if (-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $protected 'Supertool.exe'))) { throw 'Payload cleanup guard failed.' }
$owned = Join-Path $root ('build\installer-build-' + ('a' * 32))
[void][IO.Directory]::CreateDirectory($owned)
$link = Join-Path $owned 'link'
New-Item -ItemType Junction -Path $link -Target $protected | Out-Null
try {
    $rejected = $false
    try { Remove-InstallerWorkspace $root $owned } catch { $rejected = $_ -match 'Reparse' }
    if (-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $protected 'Supertool.exe'))) { throw 'Directory-link cleanup guard failed.' }
} finally { [IO.Directory]::Delete($link) }
Remove-InstallerWorkspace $root $owned
''')
    check_result(run(safety), True)
    clean_workspaces(root)
    print('PASS: cleanup rejects payload paths and directory links without deleting their targets', flush=True)

    redirected = root / 'scripts/output-safety.ps1'
    redirected.write_text(r'''
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'installer-workspace.ps1')
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $root 'payload'
$version = (Get-Content -LiteralPath (Join-Path $payload 'release.json') -Raw | ConvertFrom-Json).version
$name = 'Supertool-' + $version + '-core-win-x64-setup.exe'
foreach ($leaf in @('installer-history',($name + '.sha256'),($name + '.payload.json'))) {
    $path = Join-Path $root ('out\' + $leaf)
    $saved = Join-Path $root ('saved-' + $leaf)
    Assert-InstallerLocalPath $root $path
    Assert-InstallerLocalPath $root $saved
    Move-Item -LiteralPath $path -Destination $saved
    New-Item -ItemType Junction -Path $path -Target $payload | Out-Null
    try {
        $rejected = $false
        try { & (Join-Path $PSScriptRoot 'build-installer.ps1') -Version $version -Distribution core -PayloadDirectory $payload | Out-Null } catch { $rejected = $_ -match 'Reparse' }
        if (-not $rejected) { throw ('Output directory-link guard failed: ' + $leaf) }
    } finally {
        [IO.Directory]::Delete($path)
        Move-Item -LiteralPath $saved -Destination $path
    }
}
''')
    check_result(run(redirected), True)
    clean_workspaces(root)
    print('PASS: output sidecars and installer history reject redirected paths', flush=True)
    original_setup = hashlib.sha256(setup.read_bytes()).hexdigest()

    # Compilation fails after the payload and compiler were both unpacked.
    (root / 'installer/Supertool.iss').write_text('#error deliberate compiler failure\n')
    check_result(run(script, *args), False, b'deliberate compiler failure')
    clean_workspaces(root)
    assert hashlib.sha256(setup.read_bytes()).hexdigest() == original_setup, 'failed build replaced previous installer'
    error_log = root / 'build/installer-last-error.log'
    assert 'deliberate compiler failure' in error_log.read_text(encoding='utf-8-sig')
    print('PASS: compiler failure removes payload/tools, preserves old setup and diagnostic log', flush=True)

    (payload / 'release.json').write_text('{}')
    check_result(run(script, *args), False, b'version')
    clean_workspaces(root)
    (payload / 'release.json').write_text(json.dumps(metadata))
    (root / 'tools/installer-downloads/innounp-267.zip').write_bytes(b'corrupt cached archive')
    check_result(run(script, *args), False, b'SHA256 mismatch')
    clean_workspaces(root)
    assert (payload / 'Supertool.exe').is_file()
    print('PASS: staging/hash errors clean their temporary trees and preserve inputs', flush=True)
    (root / 'tools/installer-downloads/innosetup-6.7.3.exe').unlink()
    os.environ['SHIFTWHEEL_OFFLINE'] = '1'
    check_result(run(script, *args), False, b'Offline check: missing pinned installer tool')
    clean_workspaces(root)
    print('PASS: offline checks fail clearly on a cache miss without downloading', flush=True)

print('PASS: installer temporary-file lifecycle')
