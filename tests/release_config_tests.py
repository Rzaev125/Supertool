"""Fail early when a release entry point still defaults to another version."""
import argparse
import re
from pathlib import Path


def check(root):
    def single(path, pattern):
        matches = re.findall(pattern, (root / path).read_text(encoding='utf-8'), re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f'{path}: expected one version declaration, found {len(matches)}')
        return matches[0]

    version = single('CMakeLists.txt', r'^project\(ShiftWheel VERSION ([0-9]+\.[0-9]+\.[0-9]+) ')
    declarations = [
        ('src/updates.h', r'app_version\[\] = L"([0-9.]+)"', version),
        ('resources/app.rc', r'VALUE "ProductVersion", "([0-9.]+)\\0"', version),
        ('resources/app.rc', r'VALUE "FileVersion", "([0-9.]+)\\0"', version + '.0'),
        ('resources/app.rc', r'^ FILEVERSION ([0-9,]+)', version.replace('.', ',') + ',0'),
        ('resources/app.rc', r'^ PRODUCTVERSION ([0-9,]+)', version.replace('.', ',') + ',0'),
        ('installer/ShiftWheel.iss', r'#define AppVersion "([0-9.]+)"', version),
        ('scripts/package.ps1', r"^\$version='([0-9.]+)'", version),
        ('scripts/build-installer.ps1', r"\$Version = '([0-9.]+)'", version),
        ('scripts/make-update-manifest.ps1', r"\$Version='([0-9.]+)'", version),
        ('resources/START.txt', r'^ShiftWheel ([0-9.]+) ', version),
        ('README.md', r'^# ShiftWheel ([0-9.]+) ', version),
    ]
    errors = []
    for path, pattern, expected in declarations:
        actual = single(path, pattern)
        if actual != expected:
            errors.append(f'{path}: version {actual}, expected {expected}')
    # Inspect just the version input's indented block; other workflow inputs
    # and action versions have independent meanings and must not be matched.
    block = single('.github/workflows/release.yml', r'^      version:\s*\n((?:^        .*(?:\n|$))+)')
    defaults = re.findall(r"^        default: ['\"]([0-9.]+)['\"]\s*$", block, re.MULTILINE)
    if defaults != [version]:
        errors.append(f'.github/workflows/release.yml: version input default {defaults}, expected {version}')
    if errors:
        raise ValueError('\n'.join(errors))
    return version


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        print('PASS release version declarations and defaults: ' + check(args.root))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')
