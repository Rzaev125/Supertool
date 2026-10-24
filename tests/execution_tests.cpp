#include "execution.h"
#include <iostream>
#include <stdexcept>
using namespace shiftwheel;
void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
int main(){try{ExecutionFacts f;
check(parse_execution("SWE1\tstage\twic",f)&&f.stages==std::vector<std::string>{"wic"},"stage not recorded");
check(parse_execution("SWE1\tencoder\tlibx264\tcpu\t1",f)&&f.backend=="cpu"&&f.fallback,"actual fallback missing");
check(parse_execution("SWE1\tbytes\t123",f)&&f.output_bytes==123,"published bytes missing");
for(auto bad:{"SWE1\tstage\tinvented","SWE1\tencoder\th264_nvenc\tcpu\t0","SWE1\tbytes\t-1","SWE1\tbytes\t18446744073709551616","SWE1\tstage\twic\tinjection","SWE1\tencoder\tlibx264\tcpu\t2"})check(!parse_execution(bad,f),"invalid telemetry accepted");
check(f.stages.size()==1&&f.backend=="cpu"&&f.output_bytes==123,"invalid event changed facts");
check(execution_detail(nullptr,0,false).find(L"нет данных")!=std::wstring::npos,"legacy facts invented");
std::cout<<"execution contracts PASS\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
