// Development-only process-tree accounting; never included in the product.
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv){
 if(argc<2)return 2;std::wstring command;
 for(int i=1;i<argc;++i){if(std::wstring(argv[i]).find(L'"')!=std::wstring::npos)return 2;command+=L"\""+std::wstring(argv[i])+L"\" ";}
 auto job=CreateJobObjectW(nullptr,nullptr);if(!job)return 2;
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))return 2;
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};auto start=std::chrono::steady_clock::now();
 if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(job);return 2;}
 if(!AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,2);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(job);return 2;}
 ResumeThread(pi.hThread);CloseHandle(pi.hThread);SIZE_T peak_rss=0;
 for(;;){
  alignas(JOBOBJECT_BASIC_PROCESS_ID_LIST) unsigned char buffer[8192]{};auto list=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer);SIZE_T rss=0;
  if(QueryInformationJobObject(job,JobObjectBasicProcessIdList,buffer,sizeof(buffer),nullptr))for(DWORD i=0;i<list->NumberOfProcessIdsInList;++i){auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,static_cast<DWORD>(list->ProcessIdList[i]));if(p){PROCESS_MEMORY_COUNTERS info{sizeof(info)};if(GetProcessMemoryInfo(p,&info,sizeof(info)))rss+=info.WorkingSetSize;CloseHandle(p);}}
  if(rss>peak_rss)peak_rss=rss;
  auto wait=WaitForSingleObject(pi.hProcess,5);if(wait==WAIT_OBJECT_0)break;if(wait!=WAIT_TIMEOUT){CloseHandle(job);return 2;}
  if(std::chrono::steady_clock::now()-start>std::chrono::minutes(5)){CloseHandle(job);return 2;}
 }
 DWORD code=2;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
 JOBOBJECT_BASIC_ACCOUNTING_INFORMATION account{};QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&account,sizeof(account),nullptr);
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION peak{};QueryInformationJobObject(job,JobObjectExtendedLimitInformation,&peak,sizeof(peak),nullptr);CloseHandle(job);
 auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 std::cout<<"{\"exit\":"<<code<<",\"wall_ms\":"<<elapsed<<",\"cpu_ms\":"<<(account.TotalUserTime.QuadPart+account.TotalKernelTime.QuadPart)/10000.0<<",\"peak_commit_bytes\":"<<peak.PeakJobMemoryUsed<<",\"sampled_sum_rss_bytes\":"<<peak_rss<<"}\n";
 return code?1:0;
}
