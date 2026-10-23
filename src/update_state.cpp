#include "update_state.h"
#include "updates.h"
#include "job_store.h"
#include "file_policy.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
namespace shiftwheel {
namespace fs=std::filesystem;
int64_t update_now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool may_install_update(bool requested,bool running,size_t pending){return requested&&!running&&!pending;}
UpdatePolicy load_update_policy(const fs::path& path){
 auto text=load_local_state(path,1024);if(!text)return {};std::istringstream in(*text);std::string magic,version;UpdatePolicy p;
 if(!(in>>magic>>p.deferred_until>>p.retry_after>>p.failures>>std::quoted(version))||magic!="SWU1"||p.deferred_until<0||p.retry_after<0||p.failures>8)throw std::runtime_error("Состояние обновлений повреждено.");
 in>>std::ws;if(!in.eof())throw std::runtime_error("Состояние обновлений повреждено.");p.highest_version={version.begin(),version.end()};
 if(!version.empty())newer_version(p.highest_version,L"0.0.0");return p;
}
void save_update_policy(const fs::path& path,const UpdatePolicy& p){
 if(!p.highest_version.empty())newer_version(p.highest_version,L"0.0.0");
 std::string version;for(auto c:p.highest_version)version+=static_cast<char>(c);
 std::ostringstream o;o<<"SWU1 "<<p.deferred_until<<' '<<p.retry_after<<' '<<p.failures<<' '<<std::quoted(version)<<'\n';save_local_state(path,o.str());
}
namespace {
bool plain(const fs::path& p){auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));}
bool owned(const fs::path& dir){
 auto name=dir.filename().wstring();if(name.size()!=38||name.front()!=L'{'||name.back()!=L'}')return false;
 for(size_t i=1;i<37;++i){if(i==9||i==14||i==19||i==24){if(name[i]!=L'-')return false;}else if(!((name[i]>=L'0'&&name[i]<=L'9')||(name[i]>=L'a'&&name[i]<=L'f')||(name[i]>=L'A'&&name[i]<=L'F')))return false;}
 auto a=GetFileAttributesW(dir.c_str());if(a==INVALID_FILE_ATTRIBUTES||!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT))return false;
 if(!plain(dir/L".shiftwheel-update"))return false;
 auto marker=load_local_state(dir/L".shiftwheel-update",64);if(!marker||*marker!="ShiftWheel-update-cache-v1\n")return false;
 for(const auto& e:fs::directory_iterator(dir)){
  if(!plain(e.path()))return false;auto file=e.path().filename().wstring();
  if(file==L".shiftwheel-update"||file==L"setup.exe")continue;
  if(!file.starts_with(L"apply-")||!file.ends_with(L".exe")||file.size()<11)return false;
  auto digits=file.substr(6,file.size()-10);if(digits.empty()||digits.find_first_not_of(L"0123456789")!=std::wstring::npos)return false;
 }
 return true;
}
void erase_owned(const fs::path& dir){
 if(!owned(dir))return;
 for(const auto& e:fs::directory_iterator(dir))if(e.path().filename()!=L".shiftwheel-update"){std::error_code ec;if(!fs::remove(e.path(),ec)||ec)return;}
 std::error_code ec;fs::remove(dir/L".shiftwheel-update",ec);if(!ec)fs::remove(dir,ec);
}
}
void discard_update_download(const fs::path& setup)noexcept{try{validate_local_path(setup,false);if(setup.filename()==L"setup.exe")erase_owned(setup.parent_path());}catch(...) {}}
void cleanup_update_cache(const fs::path& root,const fs::path& protected_setup)noexcept{try{
 validate_local_path(root,false);if(!fs::exists(root))return;
 std::vector<fs::path> dirs;for(const auto& e:fs::directory_iterator(root))if(owned(e.path()))dirs.push_back(e.path());
 std::sort(dirs.begin(),dirs.end(),[](const auto& a,const auto& b){return fs::last_write_time(a)>fs::last_write_time(b);});
 for(size_t i=0;i<dirs.size();++i){if(!protected_setup.empty()&&dirs[i].lexically_normal()==protected_setup.parent_path().lexically_normal())continue;
  if(i>=2||fs::file_time_type::clock::now()-fs::last_write_time(dirs[i])>std::chrono::hours(24*7))erase_owned(dirs[i]);
 }
}catch(...) {}}
}
