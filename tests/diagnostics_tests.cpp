#include "diagnostics.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool v){if(!v)throw std::runtime_error("diagnostic assertion");}
int main(){try{
 check(file_error(ERROR_DISK_FULL,"write").find("места")!=std::string::npos);
 check(file_error(ERROR_SHARING_VIOLATION,"read").find("закройте")!=std::string::npos);
 check(file_error(ERROR_ACCESS_DENIED,"write").find("папку")!=std::string::npos);
 auto root=std::filesystem::temp_directory_path()/(L"diagnostics-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directory(root);
 DiagnosticLog log(root/L"events.log");for(int i=0;i<12000;++i)log.append("conversion-failed",i);
 check(std::filesystem::file_size(root/L"events.log")<=DiagnosticLog::max_bytes);
 log.append("C:\\private\\name.pdf\nSECRET");
 {std::ofstream injection(root/L"events.log",std::ios::app|std::ios::binary);injection<<"2026-10-23TSECRET00Z conversion-failed id=1\r\n";}
 log.export_to(root/L"report.txt");std::ifstream f(root/L"report.txt",std::ios::binary);std::string text{std::istreambuf_iterator<char>(f),{}};
 check(text.find("conversion-failed")!=std::string::npos && text.find("SECRET")==std::string::npos && text.find("private")==std::string::npos);
 {std::ofstream e(root/L"existing.txt");e<<"user data";} bool rejected=false;try{log.export_to(root/L"existing.txt");}catch(...){rejected=true;}check(rejected);
 f.close();std::filesystem::remove_all(root);std::cout<<"PASS actionable errors, bounded log, no paths/content, exclusive export\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what();return 1;}}
