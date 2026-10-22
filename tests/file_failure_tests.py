"""Real Windows locks, private directory ACL failure, Unicode and long names."""
import argparse, ctypes, hashlib, subprocess, tempfile
from ctypes import wintypes as w
from pathlib import Path
from PIL import Image

p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);exe=p.parse_args().exe.resolve()
root=Path(tempfile.mkdtemp(prefix='filesystem-',dir=Path.cwd()/'test-output'))
source=root/('Фото ə Azərbaycan 日本語 '+'a'*80+'.png')
Image.new('RGBA',(24,16),(50,120,220,140)).save(source)
original=hashlib.sha256(source.read_bytes()).digest()
def run(output,work,fail=False,hashcheck=True):
    r=subprocess.run([str(exe),'--convert',str(source),str(output),str(work),'jpg'],capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
    error=Path(str(work)+'.error.txt');message=error.read_text('utf-8') if error.exists() else ''
    if hashcheck: assert hashlib.sha256(source.read_bytes()).digest()==original
    assert not work.exists(), 'unowned or unfinished staging left behind'
    if fail: assert r.returncode!=0 and not output.exists(),message
    else: assert r.returncode==0 and output.exists(),message
    return message
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE];k.CreateFileW.restype=w.HANDLE
k.CloseHandle.argtypes=[w.HANDLE]
lock=k.CreateFileW(str(source),0x80000000,0,None,3,0,None)
assert lock!=ctypes.c_void_p(-1).value
try: assert 'закройте' in run(root/'locked.jpg',root/'locked-work',True,False)
finally:k.CloseHandle(lock)
run(root/'normal.jpg',root/'normal-work')
assert Image.open(root/'normal.jpg').size==(24,16)

# Change only the ACL of our freshly created, empty fixture directory; restore in finally.
blocked=root/'denied';blocked.mkdir()
a=ctypes.WinDLL('advapi32',use_last_error=True);ptr=ctypes.c_void_p
a.GetNamedSecurityInfoW.argtypes=[w.LPWSTR,w.DWORD,w.DWORD,ptr,ptr,ctypes.POINTER(ptr),ptr,ctypes.POINTER(ptr)]
a.SetNamedSecurityInfoW.argtypes=[w.LPWSTR,w.DWORD,w.DWORD,ptr,ptr,ptr,ptr]
a.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(ptr),ptr]
a.GetSecurityDescriptorDacl.argtypes=[ptr,ctypes.POINTER(w.BOOL),ctypes.POINTER(ptr),ctypes.POINTER(w.BOOL)]
k.LocalFree.argtypes=[ptr]
old_acl=ptr();old_sd=ptr();new_sd=ptr();new_acl=ptr();present=w.BOOL();defaulted=w.BOOL()
assert a.GetNamedSecurityInfoW(str(blocked),1,4,None,None,ctypes.byref(old_acl),None,ctypes.byref(old_sd))==0
assert a.ConvertStringSecurityDescriptorToSecurityDescriptorW('D:(D;;0x00000006;;;WD)(A;;FA;;;OW)',1,ctypes.byref(new_sd),None)
assert a.GetSecurityDescriptorDacl(new_sd,ctypes.byref(present),ctypes.byref(new_acl),ctypes.byref(defaulted))
try:
    assert a.SetNamedSecurityInfoW(str(blocked),1,0x80000004,None,None,new_acl,None)==0
    # The error report is a sibling of work, so use work beside the denied result folder:
    r=subprocess.run([str(exe),'--convert',str(source),str(blocked/'result.jpg'),str(blocked/'work'),'jpg'],capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
    assert r.returncode!=0 and not (blocked/'result.jpg').exists()
finally:
    assert a.SetNamedSecurityInfoW(str(blocked),1,0x20000004,None,None,old_acl,None)==0
    k.LocalFree(old_sd);k.LocalFree(new_sd)
assert hashlib.sha256(source.read_bytes()).digest()==original
# The denied folder also prevents creation of the error report: CLI must fail safely.
assert not list(blocked.iterdir())
print('PASS real locked source, Unicode/long filename, denied output ACL, original preservation and no partial output')
