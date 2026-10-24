#include "diagnostics.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>
namespace shiftwheel {
std::string file_error(DWORD code,std::string_view context) {
 std::string text(context);text += " ";
 switch(code){
 case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: text += "Недостаточно места на диске — освободите место или выберите другую папку.";break;
 case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION:text += "Файл занят — закройте его в другой программе и повторите.";break;
 case ERROR_ACCESS_DENIED:case ERROR_PRIVILEGE_NOT_HELD:text += "Нет доступа — выберите доступную для записи папку и проверьте права.";break;
 case ERROR_FILE_NOT_FOUND:case ERROR_PATH_NOT_FOUND:text += "Файл или папка не найдены — проверьте путь и повторите.";break;
 case ERROR_FILENAME_EXCED_RANGE:text += "Путь слишком длинный — переместите файл ближе к корню диска или сократите имя.";break;
 case ERROR_FILE_EXISTS:case ERROR_ALREADY_EXISTS:text += "Имя уже занято — повторите задачу, чтобы выбрать свободное имя.";break;
 default:text += "Проверьте доступ к файлу и папке результата и повторите. Код Windows: "+std::to_string(code)+".";
 }
 return text;
}
namespace {struct File{HANDLE h=INVALID_HANDLE_VALUE;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};}
void DiagnosticLog::append(std::string_view event,unsigned long long id) noexcept {
 try {
  bool accepted=false;for(auto allowed:{"session-start","conversion-queued","conversion-succeeded","conversion-failed","conversion-cancelled","conversion-start-failed","admission-rejected"})if(event==allowed)accepted=true;
  if(!accepted)return;
  File file{CreateFileW(file_.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(file.h==INVALID_HANDLE_VALUE)return;
  BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))return;
  SYSTEMTIME time{};GetSystemTime(&time);char line[256]{};
  const auto size=snprintf(line,sizeof(line),"%04u-%02u-%02uT%02u:%02u:%02uZ %.*s id=%llu\r\n",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,static_cast<int>(event.size()),event.data(),id);
  if(size<=0||size>=static_cast<int>(sizeof(line)))return;
  LARGE_INTEGER length{};if(!GetFileSizeEx(file.h,&length))return;
  if(length.QuadPart+size>max_bytes){SetFilePointer(file.h,0,nullptr,FILE_BEGIN);if(!SetEndOfFile(file.h))return;}else SetFilePointer(file.h,0,nullptr,FILE_END);
  DWORD written=0;WriteFile(file.h,line,static_cast<DWORD>(size),&written,nullptr);
 } catch(...) {}
}
void DiagnosticLog::export_to(const std::filesystem::path& destination) const {
 File source{CreateFileW(file_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
 std::string report="Supertool diagnostics (events only; no document paths or content)\r\n";
 if(source.h!=INVALID_HANDLE_VALUE){BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(source.h,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nFileSizeHigh||info.nFileSizeLow>max_bytes)throw std::runtime_error("Журнал диагностики недоступен.");
  // Only export the fixed event vocabulary produced above, not arbitrary log content.
  std::string data(info.nFileSizeLow,'\0');DWORD got=0;if(!ReadFile(source.h,data.data(),static_cast<DWORD>(data.size()),&got,nullptr))throw std::runtime_error("Не удалось прочитать журнал.");data.resize(got);
  size_t begin=0;while(begin<data.size()){auto end=data.find('\n',begin);if(end==std::string::npos)break;auto line=data.substr(begin,end-begin);begin=end+1;
   if(line.size()<25||line.size()>200||line[4]!='-'||line[10]!='T')continue;
   bool timestamp=true;
   for(size_t i=0;i<20;++i) {
    const char separator=i==4||i==7?'-':i==10?'T':i==13||i==16?':':i==19?'Z':0;
    if(separator ? line[i]!=separator : line[i]<'0'||line[i]>'9') timestamp=false;
   }
   if(!timestamp)continue;
   auto event_begin=line.find(' ');auto event_end=line.find(" id=",event_begin);if(event_begin!=20||event_end==std::string::npos)continue;
   auto event=line.substr(event_begin+1,event_end-event_begin-1);bool accepted=false;
   for(auto allowed:{"session-start","conversion-queued","conversion-succeeded","conversion-failed","conversion-cancelled","conversion-start-failed","admission-rejected"})if(event==allowed)accepted=true;
   auto id=line.substr(event_end+4);if(!id.empty()&&id.back()=='\r')id.pop_back();
   if(accepted&&!id.empty()&&id.find_first_not_of("0123456789")==std::string::npos)report+=line+"\n";
  }
 }
 File output{CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
 if(output.h==INVALID_HANDLE_VALUE)throw std::runtime_error(file_error(GetLastError(),"Не удалось создать отчёт."));
 DWORD written=0;if(!WriteFile(output.h,report.data(),static_cast<DWORD>(report.size()),&written,nullptr)||written!=report.size()||!FlushFileBuffers(output.h)){
  const auto error=GetLastError();CloseHandle(output.h);output.h=INVALID_HANDLE_VALUE;DeleteFileW(destination.c_str());throw std::runtime_error(file_error(error,"Не удалось сохранить отчёт."));
 }
}
}