"""Restore real pinned CABs into an isolated developer tree, then reuse it."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from module_bootstrap import bootstrap, native_extractor

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--helper', type=Path, default=ROOT / 'build/Release/module_unpack.exe')
parser.add_argument('--all', action='store_true', help='Exercise all six CABs; default uses the small PDFium CAB.')
args = parser.parse_args()
catalog = json.loads((ROOT / 'resources/module_catalog.json').read_text(encoding='utf-8'))
if not args.all:
    catalog = [entry for entry in catalog if entry['id'] == 'pdfium']
started = time.monotonic()
fixture = Path(tempfile.mkdtemp(prefix='bootstrap-acceptance-', dir=os.environ.get('SHIFTWHEEL_TEST_ARTIFACT_ROOT', ROOT / 'build')))
try:
    (fixture / 'resources').mkdir()
    (fixture / 'resources/module_catalog.json').write_text(json.dumps(catalog), encoding='utf-8')
    native = native_extractor(ROOT, args.helper)
    def extract(spec, cab, destination):
        print('Restoring ' + spec['id'], flush=True)
        native(spec, cab, destination)
    first = bootstrap(fixture, ROOT / 'out/module-assets', extractor=extract)
    def no_unpack(*args):
        raise AssertionError('Repeated bootstrap must reuse correct modules.')
    second = bootstrap(fixture, ROOT / 'out/module-assets', extractor=no_unpack)
    assert len(first['restored']) == len(catalog) == len(second['reused'])
    assert not list((fixture / 'tools').glob('.bootstrap-*'))
    result = dict(first=first, second=second, seconds=round(time.monotonic()-started, 2))
except BaseException:
    print('Failed native acceptance fixture retained: ' + str(fixture), flush=True)
    raise
else:
    assert fixture.resolve().parent == Path(os.environ.get('SHIFTWHEEL_TEST_ARTIFACT_ROOT', ROOT / 'build')).resolve()
    shutil.rmtree(fixture)
result['fixtures_removed'] = not fixture.exists()
label = 'all' if args.all else 'pdfium'
(ROOT / ('build/bootstrap-native-' + label + '.json')).write_text(json.dumps(result, indent=2), encoding='utf-8')
print('PASS real CAB recovery/reuse: ' + str(len(catalog)) + ' modules; temporary copy removed.', flush=True)
