#include "modules.h"
#include "job_store.h"
#include <fstream>
#include <iostream>
using namespace shiftwheel;
namespace fs=std::filesystem;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F> void rejects(F f){bool b=false;try{f();}catch(...){b=true;}check(b,"must reject");}
int main(int argc,char** argv){try{
 for(auto s:{"../x","a/../x","/x","C:/x","a:x","a//b","a\\b","a/CON.txt","a./b","a /b","a/aux","a/COM1",".active-id","x\n"}) check(!safe_module_path(s),s);
 check(safe_module_path("program/soffice.com"),"nested path");
 auto root=fs::current_path()/(L"module-test-root-"+std::to_wstring(GetTickCount64()));fs::create_directories(root);
 auto clean=[&]{fs::remove_all(root);};
 {ModuleLease a(root,false);ModuleLease b(root,false);rejects([&]{ModuleLease c(root,true);});}
 {ModuleLease a(root,true);rejects([&]{ModuleLease b(root,false);});}
 check(module_directory(root,"ffmpeg")==root/L"modules/ffmpeg","legacy");
 fs::create_directories(root/L"modules");
 save_local_state(root/L"modules/.active-ffmpeg","../escape");rejects([&]{module_directory(root,"ffmpeg");});
 save_local_state(root/L"modules/.active-ffmpeg","disabled");check(!fs::exists(module_directory(root,"ffmpeg")/L"ffmpeg.exe"),"disabled");
 fs::remove(root/L"modules/.active-ffmpeg");
 auto fixture=fs::absolute(L"../../cab-spike/test.cab");
 if(!fs::exists(fixture)) fixture=fs::absolute(L"build/cab-spike/test.cab");
 if(fs::exists(fixture)){
   ModuleSpec spec{"fixture",L"Fixture",L"1.0.0",L"https://example.com/test.cab",file_sha256(fixture),fs::file_size(fixture),9,1,"",{"sub/a.txt"}};
   auto extract=root/L"extract";fs::create_directory(extract);extract_module_cab(fixture,extract,spec,{});
   check(fs::file_size(extract/L"sub/a.txt")==9,"real cab extraction");
   fs::create_directory(root/L"cancel");std::stop_source stop;stop.request_stop();rejects([&]{extract_module_cab(fixture,root/L"cancel",spec,stop.get_token());});
   spec.bytes++;rejects([&]{verify_module_archive(fixture,spec);});spec.bytes--;spec.sha256[0]=spec.sha256[0]==L'0'?L'1':L'0';rejects([&]{verify_module_archive(fixture,spec);});
 }
 if(argc==2){
  fs::path fixtures=argv[1];auto cab=fixtures/L"good.cab";
  ModuleSpec spec{"ffmpeg",L"Fixture",L"0.9.0",L"https://example.com/module.cab",file_sha256(cab),fs::file_size(cab),7+fs::file_size(fixtures/L"inventory.txt"),2,"",{"sub/file.txt"},file_sha256(fixtures/L"inventory.txt")};
  auto copy=[&](const auto&,auto,std::ostream& out,std::stop_token){std::ifstream in(cab,std::ios::binary);out<<in.rdbuf();};
  install_module(root,spec,{}, {},copy);check(module_status(root,spec).version==L"0.9.0","installed version");
  save_local_state(root/L"modules/.active-ffmpeg","../corrupt");
  install_module(root,spec,{}, {},copy);check(module_status(root,spec).installed,"repair corrupt pointer");
  auto current=module_directory(root,"ffmpeg");check(fs::is_regular_file(current/L"sub/file.txt"),"atomic pointer publication");
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
