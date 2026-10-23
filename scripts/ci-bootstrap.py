"""Prepare developer modules from authenticated local CABs, without downloads."""
import argparse
from pathlib import Path
import sys
from module_bootstrap import bootstrap, native_extractor

root = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=root / 'out/module-assets')
    parser.add_argument('--extractor', type=Path, help='Locally built module_unpack.exe; defaults to build/Release.')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    try:
        report = bootstrap(root, args.assets, native_extractor(root, args.extractor), args.verify_only)
        print('PASS local modules: reused=' + ','.join(report['reused']) + '; restored=' + ','.join(report['restored']))
        return 0
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    raise SystemExit(main())
