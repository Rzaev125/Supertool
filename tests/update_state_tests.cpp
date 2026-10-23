#include "update_state.h"
#include <fstream>
#include <iostream>
using namespace shiftwheel;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
 auto root=std::filesystem::current_path()/L"test-output"/(L"update-state-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root);
 UpdatePolicy p;check(p.check_allowed(100,false),"initial check blocked");p.later(100);check(!p.check_allowed(101,false)&&p.check_allowed(101,true),"deferral ignored");
 save_update_policy(root/L"state",p);auto loaded=load_update_policy(root/L"state");check(!loaded.check_allowed(102,false),"defer lost across restart");
 p.failure(200);check(!p.check_allowed(201,false),"failure loops");check(p.check_allowed(200000,false),"retry never expires");
 check(!may_install_update(true,true,0)&&!may_install_update(true,false,1)&&!may_install_update(false,false,0)&&may_install_update(true,false,0),"busy/request guard");
 auto owned=root/L"{12345678-1234-1234-1234-123456789abc}";std::filesystem::create_directory(owned);std::ofstream(owned/L".shiftwheel-update",std::ios::binary)<<"ShiftWheel-update-cache-v1\n";std::ofstream(owned/L"setup.exe")<<"old";
 auto foreign=root/L"{12345678-1234-1234-1234-123456789abd}";std::filesystem::create_directory(foreign);std::ofstream(foreign/L"setup.exe")<<"foreign";
 std::filesystem::last_write_time(owned,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*9));
 cleanup_update_cache(root,owned/L"setup.exe");check(std::filesystem::exists(owned/L"setup.exe"),"pending installer deleted");
 cleanup_update_cache(root,{});check(!std::filesystem::exists(owned)&&std::filesystem::exists(foreign/L"setup.exe"),"owned cache/foreign preservation");
 std::filesystem::remove(foreign/L"setup.exe");std::filesystem::remove(foreign);std::filesystem::remove(root/L"state");std::filesystem::remove(root);
 std::cout<<"PASS update deferral/retry persistence, install guard and owned cache\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
