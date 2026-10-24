"""Exercise actual packaging entry points on tiny isolated release trees."""
import argparse,hashlib,json,pathlib,shutil,subprocess,uuid,re,os
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--exe',type=pathlib.Path,default=ROOT/'build/Release/Supertool.exe');EXE=parser.parse_args().exe.resolve()
VERSION=re.search(r'project\(Supertool VERSION ([0-9.]+)',(ROOT/'CMakeLists.txt').read_text()).group(1)
WORK=pathlib.Path(os.environ.get('SHIFTWHEEL_TEST_ARTIFACT_ROOT',str(ROOT/'build')))/('package-policy-'+uuid.uuid4().hex)
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(script,*args):
 return subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',str(script),*map(str,args)],capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW)
def fixture(name):
 p=WORK/name
 for d in ['scripts','build/Release','tools/modules/ffmpeg','build/module-packages','out/module-assets','resources']:(p/d).mkdir(parents=True,exist_ok=True)
 for script in ['package.ps1','stage-modules.ps1']:
  if (ROOT/'scripts'/script).exists():shutil.copyfile(ROOT/'scripts'/script,p/'scripts'/script)
 shutil.copytree(ROOT/'docs',p/'docs');shutil.copyfile(ROOT/'README.md',p/'README.md')
 shutil.copyfile(ROOT/'resources/START.txt',p/'resources/START.txt');shutil.copyfile(EXE,p/'build/Release/Supertool.exe')
 engine=p/'tools/modules/ffmpeg/ffmpeg.exe';engine.write_bytes(b'engine fixture')
 license=p/'tools/modules/ffmpeg/LICENSE';license.write_bytes(b'fixture license')
 inv=p/'build/module-packages/ffmpeg.inventory';inv.write_text('SWI1\n'+digest(engine)+' '+str(engine.stat().st_size)+' ffmpeg.exe\n'+digest(license)+' '+str(license.stat().st_size)+' LICENSE\n',encoding='ascii',newline='\n')
 catalog=[dict(id='ffmpeg',version='0.9.0',inventory_sha256=digest(inv),count=3,unpacked=engine.stat().st_size+license.stat().st_size+inv.stat().st_size)]
 for path in [p/'resources/module_catalog.json',p/'out/module-assets/catalog.json']:path.write_text(json.dumps(catalog))
 return p
failures=[]
for kind in ['extra','modified','valid']:
 p=fixture(kind)
 if kind=='extra':(p/'tools/modules/ffmpeg/private.txt').write_text('personal')
 if kind=='modified':(p/'tools/modules/ffmpeg/ffmpeg.exe').write_text('modified engine')
 r=run(p/'scripts/package.ps1','-StageOnly')
 ok=r.returncode==0 if kind=='valid' else r.returncode!=0
 if not ok:failures.append(kind+': packaging accepted unexpected/modified bytes' if kind!='valid' else 'valid package rejected: '+r.stderr.decode(errors='replace'))
 if kind=='valid':
  assert ok,'valid package rejected: '+(r.stdout+r.stderr).decode(errors='replace')
  packed=p/f'out/Supertool-{VERSION}-full/modules'
  assert (packed/'ffmpeg/ffmpeg.exe').read_bytes()==b'engine fixture'
  args=['-ModuleRoot',packed,'-InventoryDirectory',p/'build/module-packages','-CatalogPath',p/'resources/module_catalog.json','-Destination',p/'build/validation-only','-PackagedSource','-ValidateOnly']
  assert run(p/'scripts/stage-modules.ps1',*args).returncode==0,'valid packaged module validation'
  (packed/'ffmpeg/private.txt').write_text('private')
  assert run(p/'scripts/stage-modules.ps1',*args).returncode!=0,'packaged extras rejected'
  (packed/'ffmpeg/private.txt').unlink();(packed/'ffmpeg/ffmpeg.exe').write_bytes(b'ENGINE fixture')
  assert run(p/'scripts/stage-modules.ps1',*args).returncode!=0,'same-size modified packaged file rejected'
 print(kind,'PASS' if ok else 'FAIL',flush=True)
# Real installer entry point must compare metadata and requested distribution before compiling.
for kind,requested,metadata in [('full-labelled-core','core','full'),('core-labelled-full','full','core'),('invalid','full','bogus'),('missing','full',None),('valid-core','core','core')]:
 p=WORK/kind;p.mkdir(parents=True)
 shutil.copyfile(EXE,p/'Supertool.exe')
 if metadata!='core':(p/'modules').mkdir()
 release=dict(version=VERSION,executable_sha256=digest(p/'Supertool.exe'))
 if metadata is not None:release['distribution']=metadata
 (p/'release.json').write_text(json.dumps(release))
 isolated=ROOT/'build/installer-tests'/(WORK.name+'-'+kind)
 assert not isolated.exists(),'test requires a fresh isolated destination'
 r=run(ROOT/'scripts/build-installer.ps1','-Version',VERSION,'-Distribution',requested,'-PayloadDirectory',p,'-TestRoot',isolated)
 # Installer TestRoot is deliberately restricted to build/installer-tests.
 # Move our own output into this check's workspace before releasing that slot.
 if isolated.exists():
  assert isolated.resolve().parent==(ROOT/'build/installer-tests').resolve()
  shutil.copytree(isolated,WORK/('installer-result-'+kind))
  shutil.rmtree(isolated)
 ok=r.returncode==0 if kind=='valid-core' else r.returncode!=0 and b'distribution' in (r.stdout+r.stderr).lower()
 if not ok:failures.append(kind+': distribution contract not enforced')
 print(kind,'PASS' if ok else 'FAIL',flush=True)
if failures:raise SystemExit('\n'.join(failures))
print('PASS: pinned full package and strict installer distribution')
