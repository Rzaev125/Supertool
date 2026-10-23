"""CI publisher: packages first, public digest checks, then update channels."""
import argparse, base64, hashlib, http.client, json, os, re, subprocess, urllib.parse, urllib.request, urllib.error
from pathlib import Path

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser()
p.add_argument('--version', required=True)
p.add_argument('--publish', action='store_true')
args = p.parse_args()
version = args.version
if not re.fullmatch(r'\d{1,5}\.\d{1,5}\.\d{1,5}', version):
    raise SystemExit('Invalid release version')
repository = os.environ.get('GITHUB_REPOSITORY', 'Rzaev125/Supertool')
if repository != 'Rzaev125/Supertool':
    raise SystemExit('Unexpected publication repository')
names = [f'ShiftWheel-{version}-win-x64-setup.exe', f'ShiftWheel-{version}-win-x64.zip',
         f'ShiftWheel-{version}-source.zip']
names += [f'ShiftWheel-{version}-core-win-x64-setup.exe', f'ShiftWheel-{version}-core-win-x64.zip']
paths = [root / 'out' / name for name in names]
paths += sorted((root / 'out/module-assets').glob('*.cab'))
paths += [root / 'out/module-assets/catalog.json']
for path in paths:
    with path.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    path.with_suffix(path.suffix + '.sha256').write_text(f'{digest}  {path.name}\n', encoding='ascii')
paths += [path.with_suffix(path.suffix + '.sha256') for path in paths]
signed = root / 'out/stable-signed.txt'
plain = root / 'out/stable.txt'
if not signed.is_file() or not plain.is_file():
    raise SystemExit('Verified signed and legacy manifests required')
paths += [signed, plain]
if not args.publish:
    print('Prepared release assets:', ', '.join(path.name for path in paths))
    raise SystemExit(0)
if os.environ.get('GITHUB_REF') != 'refs/heads/main':
    raise SystemExit('Publishing is allowed only from main')
token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
if not token:
    raise SystemExit('CI authorization unavailable')
headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
           'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'ShiftWheel-release'}
api = 'https://api.github.com/repos/' + repository
def request(method, url, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method,
                                 headers=headers | ({'Content-Type': 'application/json'} if data else {}))
    with urllib.request.urlopen(req, timeout=60) as response:
        return json.load(response)

head = os.environ.get('GITHUB_SHA') or subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
releases = request('GET', api + '/releases')
if any(r['tag_name'] == 'v' + version for r in releases):
    raise SystemExit('Release already exists; inspect it rather than overwrite')
release = request('POST', api + '/releases', {
    'tag_name': 'v' + version, 'target_commitish': head, 'name': 'ShiftWheel ' + version,
    'draft': True, 'prerelease': False,
    'body': 'Durable queue/history with explicit recovery and controlled updates. See docs/verification.md and docs/signing.md for validation and activation requirements.'})
for path in paths:
    with path.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
    url = release['upload_url'].split('{')[0] + '?name=' + urllib.parse.quote(path.name)
    parts = urllib.parse.urlsplit(url)
    conn = http.client.HTTPSConnection(parts.hostname, timeout=180, blocksize=1024 * 1024)
    with path.open('rb') as f:
        conn.request('POST', parts.path + '?' + parts.query, body=f,
                     headers=headers | {'Content-Type': 'application/octet-stream', 'Content-Length': str(path.stat().st_size)})
        response = conn.getresponse()
        raw, status = response.read(), response.status
    conn.close()
    if status != 201:
        raise SystemExit(f'Upload failed: HTTP {status}')
    asset = json.loads(raw)
    if asset['size'] != path.stat().st_size or asset.get('digest') != 'sha256:' + digest:
        raise SystemExit('Remote asset digest/size mismatch')
    print('Verified uploaded asset:', path.name, flush=True)
release = request('PATCH', api + '/releases/' + str(release['id']), {'draft': False})
# Verify unauthenticated public assets before changing either channel.
for path in paths:
    url = f'https://github.com/{repository}/releases/download/v{version}/{path.name}'
    with urllib.request.urlopen(urllib.request.Request(url, method='HEAD'), timeout=60) as response:
        if response.status != 200 or int(response.headers['Content-Length']) != path.stat().st_size:
            raise SystemExit('Public release asset unavailable')
# Signed first: old clients keep the 4-field channel; pinned clients use the signed one.
for path, name in [(signed, 'stable-signed.txt'), (plain, 'stable.txt')]:
    url = api + '/contents/updates/' + name
    try:
        existing = request('GET', url + '?ref=main')
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise
        existing = None
    body = {'message': f'Publish verified {version} update channel', 'branch': 'main',
            'content': base64.b64encode(path.read_bytes()).decode('ascii')}
    if existing:
        old = base64.b64decode(existing['content']).decode('utf-8')
        match = re.search(r'^version=(\d+\.\d+\.\d+)$', old, re.M)
        if not match or tuple(map(int, match[1].split('.'))) > tuple(map(int, version.split('.'))):
            raise SystemExit('Refusing channel downgrade or malformed existing channel')
        body['sha'] = existing['sha']
    request('PUT', url, body)
print('Published:', release['html_url'])
