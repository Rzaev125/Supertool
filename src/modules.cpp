#include "modules.h"
#include "job_store.h"
#include "file_policy.h"
#include <fdi.h>
#include <objbase.h>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <array>
#include <algorithm>
#include <cstdlib>
namespace shiftwheel {
namespace fs=std::filesystem;
namespace {
[[noreturn]] void bad(){throw std::runtime_error("Модуль повреждён или не прошёл проверку. Повторите загрузку через Настройки → Модули.");}
void stopped(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Установка модуля отменена.");}
std::string narrow(const std::wstring& s){std::string out;for(auto c:s){if(c>127)bad();out+=static_cast<char>(c);}return out;}
std::wstring wide(const std::string& s){return {s.begin(),s.end()};}
std::string unique(){GUID g{};if(FAILED(CoCreateGuid(&g)))bad();wchar_t b[40]{};StringFromGUID2(g,b,40);std::wstring v(b+1,36);CharLowerBuffW(v.data(),36);return "pkg-"+narrow(v);}
struct Extraction {fs::path cab,root;const ModuleSpec* spec;std::stop_token stop;std::set<std::string> names;std::set<HANDLE> files;unsigned long long bytes=0,written=0;bool failed=false;};
thread_local Extraction* context=nullptr;
void* DIAMONDAPI alloc(ULONG n){return std::malloc(n);}
void DIAMONDAPI release(void* p){std::free(p);}
INT_PTR DIAMONDAPI open_cab(char*,int,int){auto h=CreateFileW(context->cab.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);return h==INVALID_HANDLE_VALUE?-1:reinterpret_cast<INT_PTR>(h);}
UINT DIAMONDAPI read_cab(INT_PTR h,void* p,UINT n){DWORD got=0;if(context->stop.stop_requested()||!ReadFile(reinterpret_cast<HANDLE>(h),p,n,&got,nullptr))return static_cast<UINT>(-1);return got;}
UINT DIAMONDAPI write_file(INT_PTR h,void* p,UINT n){DWORD got=0;if(context->stop.stop_requested()||n>context->spec->unpacked-context->written||!WriteFile(reinterpret_cast<HANDLE>(h),p,n,&got,nullptr))return static_cast<UINT>(-1);context->written+=got;return got;}
int DIAMONDAPI close_file(INT_PTR h){auto handle=reinterpret_cast<HANDLE>(h);context->files.erase(handle);return CloseHandle(handle)?0:-1;}
long DIAMONDAPI seek_file(INT_PTR h,long offset,int origin){LARGE_INTEGER pos{},out{};pos.QuadPart=offset;return SetFilePointerEx(reinterpret_cast<HANDLE>(h),pos,&out,static_cast<DWORD>(origin))&&out.QuadPart<=LONG_MAX?static_cast<long>(out.QuadPart):-1;}
INT_PTR DIAMONDAPI notify(FDINOTIFICATIONTYPE type,PFDINOTIFICATION n){
 auto& c=*static_cast<Extraction*>(n->pv);
 try{
  stopped(c.stop);
  if(type==fdintNEXT_CABINET||type==fdintPARTIAL_FILE)bad();
  if(type==fdintCOPY_FILE){
   std::string name(n->psz1);if(!safe_module_path(name)||n->cb<0)bad();
   auto lower=name;for(auto& x:lower)if(x>='A'&&x<='Z')x+=32;
   if(!c.names.insert(lower).second||c.names.size()>c.spec->count||static_cast<unsigned long long>(n->cb)>c.spec->unpacked-c.bytes)bad();
   c.bytes+=static_cast<unsigned long long>(n->cb);auto p=c.root/fs::path(name);validate_local_path(p,false);fs::create_directories(p.parent_path());validate_local_path(p,false);
   auto h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)bad();c.files.insert(h);return reinterpret_cast<INT_PTR>(h);
  }
  if(type==fdintCLOSE_FILE_INFO){auto h=reinterpret_cast<HANDLE>(n->hf);if(!c.files.contains(h))bad();const bool ok=FlushFileBuffers(h)!=FALSE;c.files.erase(h);CloseHandle(h);return ok?TRUE:FALSE;}
  return 0;
 }catch(...){c.failed=true;return -1;}
}
struct InventoryRow{std::string hash;unsigned long long size;std::string path;};
std::vector<InventoryRow> inventory(const fs::path& directory,const std::wstring& pin){
 auto file=directory/L".sw-inventory";if(pin.empty()||file_sha256(file)!=pin)bad();
 auto raw=load_local_state(file,8*1024*1024);if(!raw)bad();std::istringstream in(*raw);std::string line;if(!std::getline(in,line)||line!="SWI1")bad();
 std::vector<InventoryRow> rows;std::set<std::string> unique_paths;
 while(std::getline(in,line)){
  std::istringstream s(line);InventoryRow row;if(!(s>>row.hash>>row.size)||s.get()!=' ')bad();std::getline(s,row.path);
  if(row.hash.size()!=64||row.hash.find_first_not_of("0123456789abcdef")!=std::string::npos||!safe_module_path(row.path)||row.path==".sw-inventory")bad();
  auto lower=row.path;for(auto& x:lower)if(x>='A'&&x<='Z')x+=32;
  if(!unique_paths.insert(lower).second||rows.size()>=50000)bad();rows.push_back(std::move(row));
 }
 if(rows.empty())bad();return rows;
}
void verify_files(const fs::path& dir,const ModuleSpec& spec,std::stop_token stop){
 auto rows=inventory(dir,spec.inventory_sha256);if(rows.size()+1!=spec.count)bad();
 for(auto& r:rows){stopped(stop);auto p=dir/r.path;validate_local_path(p,false);if(!fs::is_regular_file(p)||fs::file_size(p)!=r.size||file_sha256(p)!=wide(r.hash))bad();}
 for(auto& p:spec.required)if(!fs::is_regular_file(dir/p))bad();
}
// Delete only bytes from a known inventory. Unknown/modified files and legacy folders survive.
void retire(const fs::path& app,const fs::path& old,const ModuleSpec& spec) noexcept{
 try{
  if(old.parent_path()!=app/L"modules"||(!old.filename().string().starts_with("pkg-")&&old.filename()!=spec.id))return;
  auto receipt=load_local_state(old/L".sw-module",500);if(!receipt||*receipt!=spec.id+"\n"+narrow(spec.version)+"\n"+narrow(spec.inventory_sha256))return;
  auto rows=inventory(old,spec.inventory_sha256);
  for(auto& r:rows){auto p=old/r.path;validate_local_path(p,false);if(fs::is_regular_file(p)&&fs::file_size(p)==r.size&&file_sha256(p)==wide(r.hash))fs::remove(p);}
  fs::remove(old/L".sw-inventory");fs::remove(old/L".sw-module");
  std::vector<fs::path> dirs;for(auto it=fs::recursive_directory_iterator(old);it!=fs::recursive_directory_iterator();++it){auto attrs=GetFileAttributesW(it->path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){it.disable_recursion_pending();continue;}if(it->is_directory())dirs.push_back(it->path());}
  std::sort(dirs.begin(),dirs.end(),[](auto& a,auto& b){return a.native().size()>b.native().size();});std::error_code ec;for(auto& p:dirs)fs::remove(p,ec);fs::remove(old,ec);
 }catch(...){}
}
}
const std::vector<ModuleSpec>& module_catalog(){static const std::vector<ModuleSpec> modules{
#include "../resources/module_catalog.inc"
};return modules;}
bool safe_module_path(std::string_view p){
 if(p.empty()||p.size()>220||p.front()=='/'||p.find_first_of("\\:\"<>|?*\r\n\t")!=std::string_view::npos||p.starts_with(".active-")||p==".sw-module")return false;
 for(unsigned char c:p)if(c<32||c>126)return false;
 while(!p.empty()){
  auto n=p.find('/');auto part=p.substr(0,n);if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')return false;
  auto dot=part.find('.');std::string base(part.substr(0,dot));for(auto& c:base)if(c>='a'&&c<='z')c-=32;
  if(base=="CON"||base=="PRN"||base=="AUX"||base=="NUL"||(base.size()==4&&(base.starts_with("COM")||base.starts_with("LPT"))&&base[3]>='0'&&base[3]<='9'))return false;
  if(n==std::string_view::npos)break;p.remove_prefix(n+1);if(p.empty())return false;
 }return true;
}
ModuleStatus module_status(const fs::path& app,const ModuleSpec& spec){
 auto dir=module_directory(app,spec.id);ModuleStatus result;
 for(auto& p:spec.required){validate_local_path(dir/p,false);if(!fs::is_regular_file(dir/p))return result;}
 result.installed=true;auto receipt=load_local_state(dir/L".sw-module",500);
 if(receipt){auto prefix=spec.id+"\n";if(receipt->starts_with(prefix)){auto n=receipt->find('\n',prefix.size());if(n!=std::string::npos)result.version=wide(receipt->substr(prefix.size(),n-prefix.size()));}}
 if(result.version.empty())result.version=L"версия неизвестна (офлайн)";return result;
}
void verify_module_archive(const fs::path& cab,const ModuleSpec& s){validate_local_path(cab,false);if(!fs::is_regular_file(cab)||fs::file_size(cab)!=s.bytes||file_sha256(cab)!=s.sha256)bad();}
void extract_module_cab(const fs::path& cab,const fs::path& fresh,const ModuleSpec& s,std::stop_token stop){
 stopped(stop);verify_module_archive(cab,s);validate_local_path(fresh,false);if(!fs::is_directory(fresh)||!fs::is_empty(fresh))bad();
 Extraction c{cab,fresh,&s,stop};context=&c;ERF error{};auto f=FDICreate(alloc,release,open_cab,read_cab,write_file,close_file,seek_file,cpuUNKNOWN,&error);
 if(!f){context=nullptr;bad();}char name[]="module.cab",path[]="";BOOL ok=FDICopy(f,name,path,0,notify,nullptr,&c);FDIDestroy(f);
 for(auto h:c.files)CloseHandle(h);context=nullptr;stopped(stop);
 if(!ok||c.failed||c.names.size()!=s.count||c.bytes!=s.unpacked||c.written!=s.unpacked)bad();
}
void cleanup_module_downloads(const fs::path& app,std::span<const ModuleSpec> catalog) noexcept{
 try{
  ModuleLease lease(app,true);
  for(const auto& spec:catalog){
   auto dir=module_directory(app,spec.id);if(dir.parent_path()!=app/L"modules"||!dir.filename().string().starts_with("pkg-"))continue;
   auto receipt=load_local_state(dir/L".sw-module",500);
   if(!receipt||*receipt!=spec.id+"\n"+narrow(spec.version)+"\n"+narrow(spec.inventory_sha256))continue;
   auto cab=dir;cab+=L".cab";
   std::error_code ec;if(!fs::exists(cab,ec))continue;
   try{verify_module_archive(cab,spec);fs::remove(cab,ec);}catch(...){}
  }
 }catch(...){}
}
ModuleInstallResult install_module(const fs::path& app,const ModuleSpec& spec,std::stop_token stop,const UpdateProgress& progress,const UpdateTransport& transport){
 cleanup_module_downloads(app);
 stopped(stop);validate_local_path(app/L"modules",false);fs::create_directories(app/L"modules");
 auto name=unique();auto stage=app/L"modules"/name;auto cab=app/L"modules"/(name+".cab");
 if(fs::space(app).available<spec.bytes+spec.unpacked+16*1024*1024)throw std::runtime_error("Недостаточно места для загрузки и распаковки модуля.");
 if(!fs::create_directory(stage))bad();
 bool committed=false;
 try{
  {std::ofstream f(cab,std::ios::binary);if(!f)bad();if(transport)transport(spec.url,spec.bytes,f,stop);else download_asset(spec.url,spec.bytes,f,stop,progress);f.close();if(!f)bad();}
  stopped(stop);extract_module_cab(cab,stage,spec,stop);verify_files(stage,spec,stop);stopped(stop);
  save_local_state(stage/L".sw-module",spec.id+"\n"+narrow(spec.version)+"\n"+narrow(spec.inventory_sha256));
  ModuleLease lease(app,true);
  if(!spec.dependency.empty()){auto& all=module_catalog();auto it=std::find_if(all.begin(),all.end(),[&](auto& x){return x.id==spec.dependency;});if(it==all.end()||!module_status(app,*it).installed)throw std::runtime_error("Сначала установите зависимый модуль PDF.");}
  fs::path old;try{old=module_directory(app,spec.id);}catch(...){/* Preserve unknown old data; repair the pointer only. */}
  save_local_state(app/L"modules"/(".active-"+spec.id),name);committed=true;
  std::error_code cleanup_error;fs::remove(cab,cleanup_error);retire(app,old,spec);
  return {static_cast<bool>(cleanup_error)};
 }catch(...){std::error_code ec;fs::remove(cab,ec);if(!committed)remove_owned_tree(stage);throw;}
}
void remove_module(const fs::path& app,const ModuleSpec& spec){
 ModuleLease lease(app,true);
 for(auto& s:module_catalog())if(s.dependency==spec.id&&module_status(app,s).installed)throw std::runtime_error("Модуль нужен установленному Office или OCR. Сначала удалите зависимые модули.");
 fs::path old;try{old=module_directory(app,spec.id);}catch(...){/* A corrupt pointer can still be disabled safely. */}
 save_local_state(app/L"modules"/(".active-"+spec.id),"disabled");retire(app,old,spec);
}
}
