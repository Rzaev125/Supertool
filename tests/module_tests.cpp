#include "modules.h"
#include "job_store.h"
#include <fstream>
#include <iostream>
#include <array>
using namespace shiftwheel;
namespace fs=std::filesystem;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F> void rejects(F f){bool b=false;try{f();}catch(...){b=true;}check(b,"must reject");}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("Run via module_fixtures.ps1 with its fixture directory");
 for(auto s:{"../x","a/../x","/x","C:/x","a:x","a//b","a\\b","a/CON.txt","a./b","a /b","a/aux","a/COM1",".active-id","x\n"}) check(!safe_module_path(s),s);
 check(safe_module_path("program/soffice.com"),"nested path");
 auto root=fs::absolute(fs::path(argv[1]))/(L"module-test-root-"+std::to_wstring(GetTickCount64()));fs::create_directories(root);
 auto clean=[&]{fs::remove_all(root);};
 {ModuleLease a(root,false);ModuleLease b(root,false);rejects([&]{ModuleLease c(root,true);});}
 {ModuleLease a(root,true);rejects([&]{ModuleLease b(root,false);});}
 check(module_directory(root,"ffmpeg")==root/L"modules/ffmpeg","legacy");
 fs::create_directories(root/L"modules");
 save_local_state(root/L"modules/.active-ffmpeg","../escape");rejects([&]{module_directory(root,"ffmpeg");});
 save_local_state(root/L"modules/.active-ffmpeg","disabled");check(!fs::exists(module_directory(root,"ffmpeg")/L"ffmpeg.exe"),"disabled");
 fs::remove(root/L"modules/.active-ffmpeg");
 if(argc==2){
  fs::path fixtures=argv[1];auto cab=fixtures/L"good.cab";
  ModuleSpec spec{"ffmpeg",L"Fixture",L"0.9.0",L"https://example.com/module.cab",file_sha256(cab),fs::file_size(cab),7+fs::file_size(fixtures/L"inventory.txt"),2,"",{"sub/file.txt"},file_sha256(fixtures/L"inventory.txt")};
  auto copy=[&](const auto&,auto,std::ostream& out,std::stop_token){std::ifstream in(cab,std::ios::binary);out<<in.rdbuf();};
  auto legacy=root/L"modules/ffmpeg";fs::create_directories(legacy/L"sub");fs::copy_file(fixtures/L"file.txt",legacy/L"sub/file.txt");
  std::ofstream(legacy/L"personal.txt")<<"keep";
  remove_module(root,spec);check(!module_status(root,spec).installed&&local_module_available(root,spec),"disabled local copy must be restorable");
  restore_local_module(root,spec,{});check(module_status(root,spec).installed&&module_directory(root,"ffmpeg")==legacy,"local restore did not re-enable original copy");
  check(fs::exists(legacy/L"personal.txt")&&!fs::exists(root/L"modules/.active-ffmpeg"),"local restore changed unrelated files");
  remove_module(root,spec);{ModuleLease busy(root,false);rejects([&]{restore_local_module(root,spec,{});});}
  fs::remove(legacy/L"sub/file.txt");rejects([&]{restore_local_module(root,spec,{});});check(!module_status(root,spec).installed,"missing local files activated");
  fs::copy_file(fixtures/L"file.txt",legacy/L"sub/file.txt");std::stop_source cancelled;cancelled.request_stop();rejects([&]{restore_local_module(root,spec,cancelled.get_token());});
  save_local_state(root/L"modules/.active-ffmpeg","../corrupt");rejects([&]{restore_local_module(root,spec,{});});
  save_local_state(root/L"modules/.active-ffmpeg","disabled");restore_local_module(root,spec,{});
  auto unsafe=spec;unsafe.required={"../escape"};check(!local_module_available(root,unsafe),"unsafe local restore path accepted");
  remove_module(root,spec);auto dependent=spec;dependent.dependency="pdfium";rejects([&]{restore_local_module(root,dependent,{});});check(!module_status(root,spec).installed,"missing dependency enabled local module");restore_local_module(root,spec,{});
  auto bootstrap_stage=root/L"developer-extraction";fs::create_directories(bootstrap_stage);
  extract_module_cab(cab,bootstrap_stage,spec,{},false);
  check(file_sha256(bootstrap_stage/L"sub/file.txt")==file_sha256(fixtures/L"file.txt"),"developer extraction preserves pinned bytes");
  install_module(root,spec,{}, {},copy);check(module_status(root,spec).version==L"0.9.0","installed version");
  save_local_state(root/L"modules/.active-ffmpeg","../corrupt");
  install_module(root,spec,{}, {},copy);check(module_status(root,spec).installed,"repair corrupt pointer");
  auto current=module_directory(root,"ffmpeg");check(fs::is_regular_file(current/L"sub/file.txt"),"atomic pointer publication");
  {
   WinHandle reader;fs::path retained;
   auto copy_locked=[&](const auto& url,auto size,std::ostream& out,std::stop_token stop){
    copy(url,size,out,stop);out.flush();
    for(auto& entry:fs::directory_iterator(root/L"modules"))if(entry.path().extension()==L".cab")retained=entry.path();
    reader.value=CreateFileW(retained.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(reader.value!=INVALID_HANDLE_VALUE,"CAB reader denies deletion");
   };
   auto installed=install_module(root,spec,{}, {},copy_locked);check(installed.cache_retained,"successful installation reports retained cache");
   check(module_status(root,spec).installed,"locked CAB does not fail committed installation");
   check(!fs::exists(current),"old managed module retired despite cleanup failure");
   check(fs::exists(retained),"locked CAB retained safely");
   install_module(root,spec,{}, {},copy);check(fs::exists(retained),"owned cache can survive a later replacement");
   CloseHandle(reader.value);reader.value=INVALID_HANDLE_VALUE;
   current=module_directory(root,"ffmpeg");
   // Fixture has a custom pin: production cleanup must not trust arbitrary CABs.
   cleanup_module_downloads(root);check(fs::exists(retained),"unrecognized download pin preserved");
   std::array<ModuleSpec,1> catalog{spec};cleanup_module_downloads(root,catalog);check(!fs::exists(retained),"unlocked owned CAB cleanup retried after retirement");
  }
  {ModuleLease reading(root,false);rejects([&]{install_module(root,spec,{}, {},copy);});check(module_directory(root,"ffmpeg")==current,"busy publication leaves original");}
  auto corrupt=spec;corrupt.sha256[0]=corrupt.sha256[0]==L'0'?L'1':L'0';rejects([&]{install_module(root,corrupt,{}, {},copy);});check(module_directory(root,"ffmpeg")==current,"hash failure leaves original");
  std::stop_source stop;stop.request_stop();rejects([&]{install_module(root,spec,stop.get_token(),{},copy);});
  auto dependency=spec;dependency.dependency="pdfium";rejects([&]{install_module(root,dependency,{}, {},copy);});
  for(auto name:{"duplicate","traversal","collision"}){
   auto malicious=fixtures/(std::string(name)+".cab");auto bad=spec;bad.sha256=file_sha256(malicious);bad.bytes=fs::file_size(malicious);bad.count=3;bad.unpacked+=7;
   auto dir=root/name;fs::create_directory(dir);rejects([&]{extract_module_cab(malicious,dir,bad,{});});
  }
  std::ofstream(current/L"personal.txt")<<"keep";std::ofstream(current/L"sub/file.txt")<<"user edit";
  remove_module(root,spec);check(fs::exists(current/L"personal.txt")&&fs::exists(current/L"sub/file.txt"),"foreign and edited files preserved");
  check(!module_status(root,spec).installed,"removed module disabled");
  install_module(root,spec,{}, {},copy);auto managed=module_directory(root,"ffmpeg");remove_module(root,spec);check(!fs::exists(managed),"managed bytes released");
 }
 clean();std::cout<<"module policy, leases, pointers, CAB bounds, transactions: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
