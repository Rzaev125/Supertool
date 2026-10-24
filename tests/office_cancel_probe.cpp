// Automatic cancellation against the real Office engine and production Worker.
#include "converter.h"
#include "worker.h"
#include "engines.h"
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <iterator>
namespace fs = std::filesystem;
std::string bytes(const fs::path& path) { std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))) return 1;
    int result = 0;
    try {
        if (argc == 8 && std::wstring(argv[1]) == L"--convert") {
            shiftwheel::convert_file(argv[2],argv[3],argv[4],shiftwheel::parse_format(argv[5]),shiftwheel::load_options(argv[7]));
        } else {
            if (argc != 2) throw std::runtime_error("expected DOCX fixture");
            const auto input=fs::absolute(argv[1]); const auto before=bytes(input);
            const auto foreign=input.parent_path()/L"document.pdf"; const auto foreign_before=bytes(foreign);
            shiftwheel::Worker worker;
            worker.start(input,shiftwheel::Format::Pdf,nullptr);
            fs::path staging;
            for (const auto& item:fs::directory_iterator(input.parent_path()))
                if (item.is_directory() && item.path().filename().wstring().starts_with(L".shiftwheel-")) staging=item.path();
            if (staging.empty()) throw std::runtime_error("no worker staging");
            const auto lock=staging/L"result.tmp/profile/.lock";
            bool ready=false;
            for (int i=0;i<1000;++i) { if(fs::is_regular_file(lock)){ready=true;break;} Sleep(10); }
            if(!ready) throw std::runtime_error("real Office never acquired its private profile");
            const auto started=GetTickCount64(); worker.cancel(); const auto outcome=worker.finish();
            if(outcome.success || !outcome.cancelled || fs::exists(outcome.output) || fs::exists(staging))
                throw std::runtime_error("Office cancellation published output or leaked staging");
            if(bytes(input)!=before || bytes(foreign)!=foreign_before) throw std::runtime_error("cancellation changed existing files");
            std::cout << "PASS real Office cancellation after profile lock; latency_ms=" << GetTickCount64()-started << '\n';
        }
    } catch (const std::exception& error) { std::cerr<<error.what();result=1; }
    CoUninitialize();return result;
}
