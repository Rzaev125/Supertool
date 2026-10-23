#include "module_paths.h"
#include "job_store.h"
#include "file_policy.h"
namespace shiftwheel {
namespace fs=std::filesystem;
fs::path application_directory(){std::wstring e(32768,L'\0');auto n=GetModuleFileNameW(nullptr,e.data(),static_cast<DWORD>(e.size()));if(!n||n>=e.size())throw std::runtime_error("Не найдена папка программы.");e.resize(n);return fs::path(e).parent_path();}
ModuleLease::ModuleLease(const fs::path& app,bool exclusive):handle_([&]{
 validate_local_path(app,false);
 auto p=app/L".shiftwheel-modules.lock";
 validate_local_path(p,false);
 auto h=CreateFileW(p.c_str(),GENERIC_READ|(exclusive?GENERIC_WRITE:0),exclusive?0:FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Модули заняты конвертацией или установкой. Дождитесь завершения и повторите.");
 BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(h,&i)||(i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){CloseHandle(h);throw std::runtime_error("Недопустимый файл блокировки модулей.");}return h;
}()){}
fs::path module_directory(const fs::path& app,const std::string& id){
 if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)throw std::runtime_error("Неизвестный модуль.");
 auto root=app/L"modules";auto pointer=root/(L".active-"+std::wstring(id.begin(),id.end()));
 auto active=load_local_state(pointer,100);
 if(!active)return root/id;
 if(*active=="disabled")return root/L".disabled"/id;
 if(!active->starts_with("pkg-")||active->size()!=40||active->find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)throw std::runtime_error("Повреждён указатель модуля. Откройте Настройки → Модули.");
 auto path=root/ *active;validate_local_path(path,false);return path;
}
fs::path module_directory(const std::string& id){return module_directory(application_directory(),id);}
}
