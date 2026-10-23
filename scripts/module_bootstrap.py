"""Restore developer modules from pinned local CABs. No network transport."""
from contextlib import contextmanager
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile


class InvalidModule(ValueError):
    pass


def local_path(path):
    path = Path(os.path.abspath(path))
    if str(path).startswith('\\\\'):
        raise ValueError('Only local paths are allowed.')
    for part in [*reversed(path.parents), path]:
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Reparse point in bootstrap path: ' + str(part))
    return path


def sha(path):
    with local_path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_name(name):
    if not isinstance(name, str) or not name or len(name) > 220 or name.startswith('.active-') or name in ('.sw-inventory', '.sw-module'):
        return False
    if any(ord(c) < 32 or ord(c) > 126 or c in '\\:"<>|?*' for c in name):
        return False
    return all(part and part not in ('.', '..') and not part.endswith(('.', ' ')) and not re.match(r'(?i)^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)', part) for part in name.split('/'))


def parse_inventory(raw, spec):
    if len(raw) > 8 * 1024**2 or hashlib.sha256(raw).hexdigest() != spec['inventory_sha256']:
        raise InvalidModule('Inventory SHA256 mismatch: ' + spec['id'])
    try:
        lines = raw.decode('ascii').splitlines()
    except UnicodeError as error:
        raise InvalidModule('Invalid inventory encoding.') from error
    if not lines or lines[0] != 'SWI1':
        raise InvalidModule('Invalid inventory header.')
    files, insensitive, total = {}, set(), len(raw)
    for line in lines[1:]:
        match = re.fullmatch(r'([a-f0-9]{64}) ([0-9]{1,12}) (.+)', line)
        if not match:
            raise InvalidModule('Invalid inventory row.')
        digest, size, name = match[1], int(match[2]), match[3]
        if not safe_name(name) or name.lower() in insensitive or size > 2147483647:
            raise InvalidModule('Unsafe or duplicate inventory path.')
        files[name] = (digest, size)
        insensitive.add(name.lower())
        total += size
    for name in files:
        parts = name.lower().split('/')
        if any('/'.join(parts[:i]) in insensitive for i in range(1, len(parts))):
            raise InvalidModule('File/directory collision in inventory.')
    if len(files) + 1 != spec['count'] or total != spec['unpacked'] or not files:
        raise InvalidModule('Inventory bounds mismatch: ' + spec['id'])
    if any(name not in files for name in spec['required']):
        raise InvalidModule('Required module file missing from inventory.')
    return files


def verify_tree(directory, files, embedded=False):
    directory = local_path(directory)
    if not directory.is_dir():
        raise InvalidModule('Missing module: ' + str(directory))
    found, expected = {}, {n.lower() for n in files}
    for current, folders, names in os.walk(directory, followlinks=False):
        for name in folders + names:
            local_path(Path(current) / name)
        for name in names:
            path = Path(current) / name
            relative = path.relative_to(directory).as_posix()
            if embedded and relative == '.sw-inventory':
                continue
            if relative.lower() not in expected:
                raise ValueError('Unknown module file preserved: ' + str(path))
            found[relative] = path
    if set(found) != set(files):
        raise InvalidModule('Missing or noncanonical module files: ' + str(directory))
    for name, path in found.items():
        digest, size = files[name]
        if path.stat().st_size != size or sha(path) != digest:
            raise InvalidModule('Modified module file: ' + str(path))


def catalog(root):
    rows = json.loads(local_path(root / 'resources/module_catalog.json').read_text(encoding='utf-8'))
    ids = set()
    if not isinstance(rows, list) or not rows:
        raise ValueError('Missing pinned module catalog.')
    for entry in rows:
        ident = entry.get('id', '')
        if not re.fullmatch('[a-z]+', ident) or ident in ids or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', entry.get('version', '')):
            raise ValueError('Invalid pinned module identity.')
        ids.add(ident)
        for field in ['sha256', 'inventory_sha256']:
            if not re.fullmatch('[a-f0-9]{64}', entry.get(field, '')):
                raise ValueError('Invalid module pin.')
        for field, limit in [('bytes', 2147483647), ('unpacked', 16 * 1024**3), ('count', 50001)]:
            if not isinstance(entry.get(field), int) or not 0 < entry[field] <= limit:
                raise ValueError('Invalid module bounds.')
        if not isinstance(entry.get('required'), list) or not entry['required'] or any(not safe_name(n) for n in entry['required']):
            raise ValueError('Unsafe required module paths.')
    for entry in rows:
        seen, dependency = {entry['id']}, entry.get('dependency', '')
        while dependency:
            if dependency in seen or dependency not in ids:
                raise ValueError('Invalid module dependency graph.')
            seen.add(dependency)
            dependency = next(r.get('dependency', '') for r in rows if r['id'] == dependency)
    return rows


@contextmanager
def bootstrap_lock(tools):
    with local_path(tools / '.bootstrap.lock').open('a+b') as handle:
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


def native_extractor(root, helper=None):
    helper = local_path(helper or root / 'build/Release/module_unpack.exe')
    def extract(spec, cab, destination):
        if not helper.is_file():
            raise ValueError('Build the local CAB helper first: cmake --build build --config Release --target module_unpack')
        try:
            result = subprocess.run([str(helper), spec['id'], str(cab), str(destination)], capture_output=True, timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired as error:
            raise ValueError('Local CAB extraction timed out: ' + spec['id'] + '. The previous module tree was not replaced.') from error
        if result.returncode:
            raise ValueError('CAB extraction failed for ' + spec['id'] + ': ' + result.stderr.decode('utf-8', errors='replace'))
    return extract


def bootstrap(root, assets=None, extractor=None, verify_only=False):
    root = local_path(root)
    assets = local_path(assets or root / 'out/module-assets')
    rows = catalog(root)
    tools = local_path(root / 'tools')
    tools.mkdir(exist_ok=True)
    destination = local_path(tools / 'modules')
    cache = local_path(root / 'build/module-packages')
    extract = extractor or native_extractor(root)
    result = dict(reused=[], restored=[])
    with bootstrap_lock(tools):
        if destination.exists():
            for entry in destination.iterdir():
                local_path(entry)
                if not entry.is_dir() or entry.name not in {r['id'] for r in rows}:
                    raise ValueError('Unknown module root entry preserved: ' + str(entry))
        inventories, valid = {}, {}
        for spec in rows:
            inv = local_path(cache / (spec['id'] + '.inventory'))
            try:
                if not inv.is_file() or inv.stat().st_size > 8 * 1024**2:
                    raise InvalidModule('Missing inventory cache.')
                raw = inv.read_bytes()
                files = parse_inventory(raw, spec)
                inventories[spec['id']] = (raw, files)
                verify_tree(destination / spec['id'], files)
                valid[spec['id']] = True
            except (InvalidModule, FileNotFoundError):
                valid[spec['id']] = False
        if all(valid.values()):
            result['reused'] = [r['id'] for r in rows]
            return result
        if verify_only:
            raise InvalidModule('Missing or modified modules: ' + ', '.join(k for k, v in valid.items() if not v))
        for spec in rows:
            if valid[spec['id']]:
                continue
            cab = local_path(assets / ('ShiftWheel-module-' + spec['id'] + '-' + spec['version'] + '.cab'))
            if not cab.is_file():
                raise ValueError('Missing local CAB: ' + str(cab) + '. Restore the pinned CAB from your local backup; no download was attempted.')
            if cab.stat().st_size != spec['bytes'] or sha(cab) != spec['sha256']:
                raise ValueError('CAB SHA256/size mismatch: ' + spec['id'])
        stage = Path(tempfile.mkdtemp(prefix='.bootstrap-', dir=tools))
        previous, incoming = stage / 'previous', stage / 'incoming'
        incoming.mkdir()
        keep = False
        try:
            for spec in rows:
                ident, target = spec['id'], incoming / spec['id']
                if valid[ident]:
                    shutil.copytree(destination / ident, target)
                    result['reused'].append(ident)
                    verify_tree(target, inventories[ident][1])
                else:
                    target.mkdir()
                    cab = assets / ('ShiftWheel-module-' + ident + '-' + spec['version'] + '.cab')
                    extract(spec, cab, target)
                    inv = local_path(target / '.sw-inventory')
                    if not inv.is_file() or inv.stat().st_size > 8 * 1024**2:
                        raise InvalidModule('Missing extracted inventory: ' + ident)
                    raw = inv.read_bytes()
                    files = parse_inventory(raw, spec)
                    verify_tree(target, files, embedded=True)
                    if (destination / ident).exists():
                        try:
                            verify_tree(destination / ident, files)
                        except InvalidModule:
                            pass  # Known damaged bytes can be repaired; unknown files cannot.
                    inv.unlink()
                    inventories[ident] = (raw, files)
                    result['restored'].append(ident)
            had_previous = destination.exists()
            try:
                if had_previous:
                    os.rename(destination, previous)
                os.rename(incoming, destination)
            except BaseException:
                if previous.exists() and not destination.exists():
                    try:
                        os.rename(previous, destination)
                    except OSError:
                        keep = True
                        raise OSError('Rollback failed. Previous modules preserved at ' + str(previous))
                raise
            cache.mkdir(parents=True, exist_ok=True)
            for ident, (raw, _) in inventories.items():
                target = local_path(cache / (ident + '.inventory'))
                temporary = cache / (ident + '-' + stage.name + '.tmp')
                try:
                    with temporary.open('xb') as stream:
                        stream.write(raw)
                    os.replace(temporary, target)
                finally:
                    if temporary.exists():
                        temporary.unlink()
            return result
        finally:
            if previous.exists() and not destination.exists():
                keep = True  # Also protect interruption between transaction steps.
            if not keep:
                for current, folders, files in os.walk(stage, followlinks=False):
                    for name in folders + files:
                        local_path(Path(current) / name)
                shutil.rmtree(stage)
