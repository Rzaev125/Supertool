"""Reuse the exact reviewed 0.7 engine bundle; never run unverified downloads."""
import argparse, hashlib, shutil, urllib.request, zipfile
from pathlib import Path

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser()
p.add_argument('--archive', type=Path)
args = p.parse_args()
archive = args.archive or root / 'build/ci-engines-0.7.0.zip'
archive.parent.mkdir(parents=True, exist_ok=True)
if not archive.exists():
    urllib.request.urlretrieve('https://github.com/Rzaev125/Supertool/releases/download/v0.7.0/ShiftWheel-0.7.0-win-x64.zip', archive)
with archive.open('rb') as f:
    if hashlib.file_digest(f, 'sha256').hexdigest() != '1e753962840044cba7ea53cf0057068a481cc945e8d3f04816a8db585bb2d52a':
        raise SystemExit('Engine archive SHA256 mismatch')
prefix = 'ShiftWheel/modules/'
with zipfile.ZipFile(archive) as bundle:
    for item in bundle.infolist():
        if not item.filename.startswith(prefix) or item.is_dir():
            continue
        relative = Path(item.filename[len(prefix):])
        if relative.is_absolute() or '..' in relative.parts or ':' in str(relative):
            raise SystemExit('Unsafe engine archive path')
        target = root / 'tools/modules' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with bundle.open(item) as source, target.open('xb') as output:
            shutil.copyfileobj(source, output)
print('PASS verified engine bundle extracted with licenses')
