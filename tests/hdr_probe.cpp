#include "image_hdr.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 2;
    int result = 0;
    try {
        const std::wstring transfer = argv[3];
        shiftwheel::hdr_to_sdr(argv[1], argv[2], transfer == L"pq" ? shiftwheel::HdrTransfer::Pq :
            transfer == L"hlg" ? shiftwheel::HdrTransfer::Hlg : shiftwheel::HdrTransfer::None);
    } catch (const std::exception& error) { std::cerr << error.what(); result = 1; }
    CoUninitialize();
    return result;
}
