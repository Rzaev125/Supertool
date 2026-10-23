#include "updates.h"
#include "update_state.h"
#include "update_signature.h"
#include "update_trust.h"
#include "job_store.h"
#include "file_policy.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <array>
#include <map>
#include <fstream>
#include <sstream>
#include <charconv>
#include <algorithm>
#include <stdexcept>
namespace shiftwheel {
namespace fs=std::filesystem;
namespace {
[[noreturn]] void fail(){throw std::runtime_error("Не удалось проверить или загрузить обновление.");}
std::wstring widen(const std::string& s){return {s.begin(),s.end()};}
struct Http { HINTERNET h; ~Http(){if(h)WinHttpCloseHandle(h);} };
std::array<unsigned,3> version(const std::wstring& s){
 std::array<unsigned,3> v{}; size_t p=0;
 for(size_t i=0;i<3;++i){size_t n=0; while(p<s.size() && s[p]>=L'0'&&s[p]<=L'9'){if(++n>5)fail();v[i]=v[i]*10+static_cast<unsigned>(s[p++]-L'0');} if(!n)fail(); if(i<2){if(p==s.size()||s[p++]!=L'.')fail();}}
 if(p!=s.size())fail(); return v;
}
fs::path local(){PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p)))fail();fs::path r(p);CoTaskMemFree(p);return r;}
std::wstring command(const fs::path& p){ if(!p.is_absolute()||p.wstring().find(L'"')!=std::wstring::npos||p.wstring().size()>240)fail();return L"\""+p.wstring()+L"\" --startup"; }
constexpr auto run_key=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
void get(const std::wstring& url,unsigned long long limit,std::ostream& output,std::stop_token stop,const UpdateProgress& progress={}){
 if(!https_url(url))fail(); URL_COMPONENTS u{sizeof(u)};u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=static_cast<DWORD>(-1);
 if(!WinHttpCrackUrl(url.c_str(),0,0,&u))fail();std::wstring host(u.lpszHostName,u.dwHostNameLength),path(u.lpszUrlPath,u.dwUrlPathLength); if(u.dwExtraInfoLength)path.append(u.lpszExtraInfo,u.dwExtraInfoLength);
 Http session{WinHttpOpen(L"ShiftWheel/0.8",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};if(!session.h)fail();WinHttpSetTimeouts(session.h,5000,5000,5000,5000);
 Http conn{WinHttpConnect(session.h,host.c_str(),u.nPort,0)};if(!conn.h)fail();
 Http req{WinHttpOpenRequest(conn.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};if(!req.h)fail();
 DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP; if(!WinHttpSetOption(req.h,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)))fail();
 DWORD auth=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;WinHttpSetOption(req.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&auth,sizeof(auth));
 if(stop.stop_requested()||!WinHttpSendRequest(req.h,nullptr,0,nullptr,0,0,0)||!WinHttpReceiveResponse(req.h,nullptr))fail();
 DWORD status=0,n=sizeof(status);if(!WinHttpQueryHeaders(req.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&n,nullptr)||status!=200)fail();
 std::array<char,65536> buf{};unsigned long long total=0;ULONGLONG start=GetTickCount64();
 for(;;){DWORD read=0;if(stop.stop_requested()||GetTickCount64()-start>20*60*1000||!WinHttpReadData(req.h,buf.data(),static_cast<DWORD>(buf.size()),&read))fail();if(!read)break;total+=read;if(total>limit)fail();output.write(buf.data(),read);if(!output)fail();if(progress)progress(total,limit);}
}
}
bool https_url(const std::wstring& url){
 if(url.size()>4096||!url.starts_with(L"https://")||url.find_first_of(L"\r\n\t #\\\"")!=std::wstring::npos)return false;
 URL_COMPONENTS u{sizeof(u)};u.dwHostNameLength=u.dwUserNameLength=u.dwPasswordLength=static_cast<DWORD>(-1);
 return WinHttpCrackUrl(url.c_str(),0,0,&u)&&u.nScheme==INTERNET_SCHEME_HTTPS&&u.dwHostNameLength>0&&!u.dwUserNameLength&&!u.dwPasswordLength;
}
bool newer_version(const std::wstring& a,const std::wstring& b){return version(a)>version(b);}
UpdateRelease parse_update_manifest(const std::string& text){
 if(text.empty()||text.size()>16384||text.find('\0')!=std::string::npos)fail();std::map<std::string,std::string> fields;std::istringstream in(text);std::string line;
 while(std::getline(in,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto eq=line.find('=');if(eq==std::string::npos||!fields.emplace(line.substr(0,eq),line.substr(eq+1)).second)fail();}
 if(fields.size()!=(fields.contains("notes")?5u:4u)||!fields.contains("version")||!fields.contains("url")||!fields.contains("size")||!fields.contains("sha256"))fail();
 UpdateRelease r{widen(fields["version"]),widen(fields["url"]),widen(fields["sha256"])};version(r.version);if(!https_url(r.url)||r.sha256.size()!=64)fail();
 for(auto& c:r.sha256){if(c>=L'A'&&c<=L'F')c+=L'a'-L'A';if(!((c>=L'a'&&c<=L'f')||(c>=L'0'&&c<=L'9')))fail();}
 auto& size=fields["size"];auto parsed=std::from_chars(size.data(),size.data()+size.size(),r.size);if(parsed.ec!=std::errc{}||parsed.ptr!=size.data()+size.size()||!r.size||r.size>1024ULL*1024*1024)fail();
 if(fields.contains("notes")){auto& notes=fields["notes"];if(notes.size()>2048)fail();for(auto c:notes)if(static_cast<unsigned char>(c)<32)fail();
  if(!notes.empty()){int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,notes.data(),static_cast<int>(notes.size()),nullptr,0);if(!count)fail();r.notes.resize(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,notes.data(),static_cast<int>(notes.size()),r.notes.data(),count);}}
 return r;
}
std::wstring file_sha256(const fs::path& path){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{alg,hash};
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0)fail();
 std::ifstream f(path,std::ios::binary);if(!f)fail();std::array<char,65536>b{};
 while(f){f.read(b.data(),b.size());if(f.gcount()&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(b.data()),static_cast<ULONG>(f.gcount()),0)<0)fail();}if(!f.eof())fail();
 std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),32,0)<0)fail();std::wstring result;for(auto byte:digest){result+=L"0123456789abcdef"[byte>>4];result+=L"0123456789abcdef"[byte&15];}return result;
}
UpdateRelease fetch_update(const std::wstring& channel,std::stop_token stop){
 std::ostringstream s;get(channel,16384,s,stop);
 if(update_signature_configured())return parse_signed_update_manifest(s.str(),SHIFTWHEEL_UPDATE_PUBLIC_KEY,SHIFTWHEEL_UPDATE_KEY_ID,app_version);
 return parse_update_manifest(s.str());
}
fs::path download_update(const UpdateRelease& r,std::stop_token stop,const UpdateTransport& transport,const UpdateProgress& progress){
 GUID id{};if(FAILED(CoCreateGuid(&id)))fail();wchar_t name[64]{};StringFromGUID2(id,name,64);auto dir=local()/L"ShiftWheel"/L"Updates"/name;
 validate_local_path(dir,false);cleanup_update_cache(dir.parent_path(),{});
 if(!fs::create_directories(dir))fail();auto path=dir/L"setup.exe";
 try{save_local_state(dir/L".shiftwheel-update","ShiftWheel-update-cache-v1\n");std::ofstream f(path,std::ios::binary);if(!f)fail();if (transport) transport(r.url,r.size,f,stop); else get(r.url,r.size,f,stop,progress);f.close();if(stop.stop_requested()||fs::file_size(path)!=r.size||file_sha256(path)!=r.sha256)fail();if(progress)progress(r.size,r.size);return path;}catch(...){discard_update_download(path);throw;}
}
bool installed_copy(const fs::path& e){
#ifdef SHIFTWHEEL_UPDATE_TESTING
 wchar_t root[32768]{}; if(GetEnvironmentVariableW(L"SHIFTWHEEL_UPDATE_TEST_ROOT",root,32768)) return _wcsicmp(e.c_str(),(fs::path(root)/L"ShiftWheel.exe").c_str())==0;
#endif
 return _wcsicmp(e.lexically_normal().c_str(),(local()/L"Programs"/L"ShiftWheel"/L"ShiftWheel.exe").c_str())==0;}
bool startup_enabled(const fs::path& e){if (!e.is_absolute() || e.wstring().size()>240 || e.wstring().find(L'"')!=std::wstring::npos) return false; wchar_t buf[1024]{};DWORD size=sizeof(buf),type=0;return RegGetValueW(HKEY_CURRENT_USER,run_key,L"ShiftWheel",RRF_RT_REG_SZ,&type,buf,&size)==ERROR_SUCCESS&&command(e)==buf;}
void set_startup(const fs::path& e,bool enabled){
 if(!enabled&&!startup_enabled(e))return;HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,run_key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)fail();
 auto value=command(e);auto result=enabled?RegSetValueExW(key,L"ShiftWheel",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t))):RegDeleteValueW(key,L"ShiftWheel");RegCloseKey(key);if(result!=ERROR_SUCCESS)fail();
}
void launch_update_helper(const fs::path& installer,const fs::path& executable,const std::wstring& hash){
 if(!installed_copy(executable)||file_sha256(installer)!=hash)fail();auto helper=installer.parent_path()/(L"apply-"+std::to_wstring(GetTickCount64())+L".exe");fs::copy_file(executable,helper,fs::copy_options::overwrite_existing);
 auto args=L"\""+helper.wstring()+L"\" --apply-update "+std::to_wstring(GetCurrentProcessId())+L" \""+installer.wstring()+L"\" \""+executable.wstring()+L"\" "+hash;
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(helper.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,helper.parent_path().c_str(),&si,&pi))fail();CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
}
int apply_update(int argc,wchar_t** argv){
 if(argc!=6)return 2;
 fs::path setup=argv[3],exe=argv[4];
 try { if(!installed_copy(exe))return 2; } catch(...) { return 2; }
 auto restart=[&](bool failed){ShellExecuteW(nullptr,L"open",exe.c_str(),failed?L"--update-failed":L"--startup",exe.parent_path().c_str(),SW_SHOWNORMAL);};
 try {
  wchar_t* end=nullptr;auto pid=wcstoul(argv[2],&end,10);if(!pid||*end)return 2;
  HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
  if(parent){auto status=WaitForSingleObject(parent,30000);CloseHandle(parent);if(status!=WAIT_OBJECT_0)return 1;}
  if(setup.filename()!=L"setup.exe"||!setup.is_absolute()||file_sha256(setup)!=argv[5]){restart(true);return 2;}
  SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;info.lpFile=setup.c_str();info.lpParameters=L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";info.nShow=SW_HIDE;
  if(!ShellExecuteExW(&info)||!info.hProcess){restart(true);return 1;}
  WaitForSingleObject(info.hProcess,INFINITE);DWORD result=1;GetExitCodeProcess(info.hProcess,&result);CloseHandle(info.hProcess);
  restart(result!=0);if(!result){std::error_code ec;fs::remove(setup,ec);}return result?1:0;
 }catch(...){restart(true);return 1;}
}
}
