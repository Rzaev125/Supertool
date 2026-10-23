// Developer helper: reuse authenticated and bounded FDI extraction from the app.
#include "modules.h"
#include <iostream>
#include <stdexcept>
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc != 4) throw std::runtime_error("Usage: module_unpack ID LOCAL_CAB EMPTY_DIRECTORY");
        for (const auto& spec : shiftwheel::module_catalog()) {
            if (std::wstring(argv[1]) != std::wstring(spec.id.begin(), spec.id.end())) continue;
            shiftwheel::extract_module_cab(argv[2], argv[3], spec, {}, false);
            std::cout << "Unpacked pinned CAB: " << spec.id << '\n';
            return 0;
        }
        throw std::runtime_error("Unknown pinned module ID.");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
