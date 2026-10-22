#include "worker.h"
#include "engines.h"
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <iterator>
namespace fs = std::filesystem;
int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--grandchild") {
        HANDLE file = CreateFileW(argv[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 2;
        DWORD size = 0, pid = GetCurrentProcessId(); WriteFile(file, &pid, sizeof(pid), &size, nullptr); FlushFileBuffers(file);
        Sleep(INFINITE); return 0;
    }
    if ((argc == 5 || argc == 6 || argc == 8) && std::wstring(argv[1]) == L"--convert") {
        shiftwheel::report_progress(42, L"Проверка отмены");
        auto nested = fs::path(argv[4]) / L"profile"; fs::create_directories(nested);
        std::wstring command = shiftwheel::quote_argument(argv[0]) + L" --grandchild " + shiftwheel::quote_argument((nested / L"locked.tmp").wstring());
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE); startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (!CreateProcessW(argv[0], command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return 3;
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
        Sleep(INFINITE); return 0;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto root = fs::current_path() / "test-output" / (L"worker-cancel-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    auto input = root / "input.png"; { std::ofstream f(input); f << "source"; }
    shiftwheel::Worker worker;
    int result = 0;
    try {
        worker.start(input, shiftwheel::Format::Jpeg, nullptr);
        fs::path staging;
        for (const auto& item : fs::directory_iterator(root)) if (item.is_directory()) staging = item.path();
        if (!fs::is_regular_file(staging / L"options.ini")) throw std::runtime_error("worker did not persist its options snapshot");
        auto locked = staging / L"result.tmp" / L"profile" / L"locked.tmp";
        for (int attempt = 0; attempt < 100 && !fs::exists(locked); ++attempt) Sleep(20);
        if (staging.empty() || !fs::exists(locked)) throw std::runtime_error("controlled grandchild did not reach writing");
        for (int attempt = 0; attempt < 100 && worker.progress().percent != 42; ++attempt) Sleep(5);
        if (worker.progress().percent != 42) throw std::runtime_error("controlled child did not deliver progress");
        auto output = root / "input.jpg";
        { std::ofstream f(output); f << "foreign file"; }
        worker.cancel();
        auto outcome = worker.finish();
        if (outcome.success || !outcome.cancelled) throw std::runtime_error("foreign output was reported as our successful conversion");
        std::ifstream f(output); std::string content{std::istreambuf_iterator<char>(f), {}};
        if (content != "foreign file" || !fs::exists(input)) throw std::runtime_error("foreign output or original modified");
        if (fs::exists(staging)) throw std::runtime_error("cancelled job staging was not cleaned");
        std::cout << "PASS cancel terminates descendants, preserves foreign output and source, removes nested staging\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; result = 1; }
    worker.shutdown(); shiftwheel::remove_owned_tree(root); CoUninitialize(); return result;
}
