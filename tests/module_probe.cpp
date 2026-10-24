// Development-only acceptance tool, with the same pinned catalogue as the UI.
#include "modules.h"
#include <fstream>
#include <iostream>
using namespace shiftwheel;
int wmain(int argc,wchar_t** argv){try{
 if(argc!=4)return 2;
 std::filesystem::path root=argv[1],assets=argv[2];std::filesystem::create_directories(root);
 for(auto& spec:module_catalog()){
  if(std::wstring(argv[3])!=L"all"&&std::wstring(argv[3])!=std::wstring(spec.id.begin(),spec.id.end()))continue;
  if(assets==L"--restore-local"){
   restore_local_module(root,spec,{});if(!module_status(root,spec).installed)throw std::runtime_error("local copy not restored");
   std::cout<<spec.id<<": existing local copy enabled; no download or copy\n";continue;
  }
  UpdateTransport transport;
  if(assets!=L"online")transport=[&](auto&,auto,std::ostream& out,auto){
   const auto suffix=std::wstring(spec.id.begin(),spec.id.end())+L"-"+spec.version+L".cab";
   auto asset=assets/(L"Supertool-module-"+suffix);
   if(!std::filesystem::exists(asset))asset=assets/(L"ShiftWheel-module-"+suffix);
   std::ifstream f(asset,std::ios::binary);if(!f)throw std::runtime_error("asset missing");out<<f.rdbuf();
  };
  install_module(root,spec,{}, {},transport);if(!module_status(root,spec).installed)throw std::runtime_error("not installed");
  std::cout<<spec.id<<": verified installed\n"<<std::flush;
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
