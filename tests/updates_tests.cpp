#include "updates.h"
#include <shlobj.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool v) { if (!v) throw std::runtime_error("update policy assertion"); }
template<class F> void reject(F f) { bool failed=false; try { f(); } catch (...) { failed=true; } check(failed); }
int wmain(int argc,wchar_t** argv) { try {
 if(argc==3 && std::wstring(argv[1])==L"--manifest") {std::ifstream f(argv[2],std::ios::binary);auto r=parse_update_manifest({std::istreambuf_iterator<char>(f),{}});std::wcout<<L"PASS manifest "<<r.version<<L" "<<r.size<<L" bytes\n";return 0;}

 if(argc>1 && std::wstring(argv[1])==L"--apply-update") return apply_update(argc,argv);
 if(argc>1 && (std::wstring(argv[1])==L"--startup" || std::wstring(argv[1])==L"--update-failed")) { wchar_t dir[32768]{};GetEnvironmentVariableW(L"SHIFTWHEEL_UPDATE_TEST_ROOT",dir,32768);std::ofstream(std::filesystem::path(dir)/L"restarted.txt")<<"ok";if(std::wstring(argv[1])==L"--update-failed")std::ofstream(std::filesystem::path(dir)/L"failed.txt")<<"ok";return 0; }
 if(argc==4 && std::wstring(argv[1])==L"--handoff-child") { launch_update_helper(argv[2],argv[3],file_sha256(argv[2]));Sleep(1000);return 0; }

 check(newer_version(L"0.10.0",L"0.6.0")); check(!newer_version(L"0.6.0",L"0.6.0")); check(!newer_version(L"0.5.9",L"0.6.0"));
 reject([]{ newer_version(L"1.2.3.exe",L"0.6.0"); }); reject([]{ newer_version(L"99999999999.2.3",L"0.6.0"); });
 check(https_url(L"https://example.com/update.txt"));
 for(auto u:{L"http://example.com/x",L"https://user:pass@example.com/x",L"https://example.com/x#frag",L"file:///x",L"https://example.com/x\r\nX:y"}) check(!https_url(u));
 const std::string good="version=0.7.0\nurl=https://example.com/setup.exe\nsize=3\nsha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n";
 auto r=parse_update_manifest(good); check(r.size==3 && r.version==L"0.7.0");
 auto described=parse_update_manifest(good+"notes=Queue recovery and safer updates\n");check(described.notes==L"Queue recovery and safer updates");
 reject([&]{parse_update_manifest(good+"version=0.8.0\n");}); reject([&]{parse_update_manifest(good+"args=/evil\n");});
 reject([]{parse_update_manifest("version=1.0.0\n");}); reject([]{parse_update_manifest(std::string(17000,'a'));});
 auto path=std::filesystem::temp_directory_path()/(L"update-hash-"+std::to_wstring(GetCurrentProcessId()));
 {std::ofstream f(path,std::ios::binary); f<<"abc";} check(file_sha256(path)==r.sha256); std::filesystem::remove(path);
 check(!installed_copy(L"C:\\portable\\Supertool.exe"));
 check(!startup_enabled(std::filesystem::path(L"C:\\"+std::wstring(250,L'a')+L"\\Supertool.exe")));
 auto transport=[](const std::wstring&,unsigned long long limit,std::ostream& out,std::stop_token){check(limit==3);out<<"abc";};
 unsigned progress_events=0;
 std::cerr<<"download test\n"; auto downloaded=download_update(r,{},transport,[&](auto got,auto total){check(got<=total);++progress_events;}); check(progress_events>0);check(file_sha256(downloaded)==r.sha256);std::filesystem::remove(downloaded.parent_path()/L".shiftwheel-update");std::filesystem::remove(downloaded);std::filesystem::remove(downloaded.parent_path());
 auto bad=r;bad.sha256=std::wstring(64,L'0');reject([&]{download_update(bad,{},transport);});
 auto wrong=r;wrong.size=4;reject([&]{download_update(wrong,{},[](auto&,auto,std::ostream& out,auto){out<<"abc";});});
 std::stop_source stop;stop.request_stop();reject([&]{download_update(r,stop.get_token(),transport);});
 std::cerr<<"registry test\n"; HKEY isolated=nullptr;auto key=L"Software\\Supertool-UpdateTests\\"+std::to_wstring(GetCurrentProcessId());
 auto cr=RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&isolated,nullptr);std::cerr<<"create="<<cr<<"\n";check(cr==ERROR_SUCCESS);
 auto ov=RegOverridePredefKey(HKEY_CURRENT_USER,isolated);std::cerr<<"override="<<ov<<"\n";check(ov==ERROR_SUCCESS);
 auto executable=std::filesystem::path(L"C:\\Program Files\\Supertool\\Supertool.exe");
 const auto legacy_command=L"\""+executable.wstring()+L"\" --startup";
 HKEY run=nullptr;check(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&run,nullptr)==ERROR_SUCCESS);
 check(RegSetValueExW(run,L"ShiftWheel",0,REG_SZ,reinterpret_cast<const BYTE*>(legacy_command.c_str()),static_cast<DWORD>((legacy_command.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS);
 check(startup_enabled(executable));
 set_startup(executable,true);std::cerr<<"enabled="<<startup_enabled(executable)<<"\n";check(startup_enabled(executable));
 DWORD legacy_size=0;check(RegGetValueW(run,nullptr,L"ShiftWheel",RRF_RT_REG_SZ,nullptr,nullptr,&legacy_size)==ERROR_FILE_NOT_FOUND);RegCloseKey(run);
 set_startup(L"C:\\other\\Supertool.exe",false);check(startup_enabled(executable));
 set_startup(executable,false);std::cerr<<"disabled="<<startup_enabled(executable)<<"\n";check(!startup_enabled(executable));
 PWSTR local_path=nullptr;check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local_path)));
 const auto legacy_directory=std::filesystem::path(local_path)/L"Programs"/L"ShiftWheel";CoTaskMemFree(local_path);
 const auto canonical=legacy_directory/L"Supertool.exe",alias=legacy_directory/L"ShiftWheel.exe";
 set_startup(alias,true);check(startup_enabled(canonical));set_startup(canonical,false);check(!startup_enabled(alias));
 set_startup(canonical,true);check(startup_enabled(alias));set_startup(alias,false);check(!startup_enabled(canonical));
 RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(isolated);RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());

 auto root=std::filesystem::temp_directory_path()/(L"shiftwheel-handoff-"+std::to_wstring(GetCurrentProcessId()));
 std::filesystem::create_directories(root);wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
 auto app=root/L"Supertool.exe",setup=root/L"setup.exe";std::filesystem::copy_file(self,app);
 std::filesystem::copy_file(std::filesystem::path(self).parent_path()/L"update_installer_fixture.exe",setup);
 SetEnvironmentVariableW(L"SHIFTWHEEL_UPDATE_TEST_ROOT",root.c_str());
 auto args=L"\""+std::wstring(self)+L"\" --handoff-child \""+setup.wstring()+L"\" \""+app.wstring()+L"\"";
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 check(CreateProcessW(self,args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&pi)!=0);CloseHandle(pi.hThread);
 Sleep(300);check(!std::filesystem::exists(root/L"installed.txt"));
 check(WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0);CloseHandle(pi.hProcess);
 for(int i=0;i<100&&!std::filesystem::exists(root/L"restarted.txt");++i)Sleep(100);
 check(std::filesystem::exists(root/L"installed.txt") && std::filesystem::exists(root/L"restarted.txt"));
 std::filesystem::remove(root/L"restarted.txt");
 {std::ofstream invalid_setup(setup,std::ios::binary);invalid_setup<<"not an executable";}
 PROCESS_INFORMATION broken{};
 check(CreateProcessW(self,args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&si,&broken)!=0);CloseHandle(broken.hThread);
 check(WaitForSingleObject(broken.hProcess,10000)==WAIT_OBJECT_0);CloseHandle(broken.hProcess);
 for(int i=0;i<100&&!std::filesystem::exists(root/L"failed.txt");++i)Sleep(100);
 check(std::filesystem::exists(root/L"failed.txt"));
 SetEnvironmentVariableW(L"SHIFTWHEEL_UPDATE_TEST_ROOT",nullptr);
 std::cout<<"PASS real copied-helper wait, installer invocation and application restart\n";
 std::cout<<"PASS version, HTTPS, strict manifest, SHA256, streaming download/mismatch/cancellation, isolated startup registration, portable policy\n"; return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n'; return 1;} }
