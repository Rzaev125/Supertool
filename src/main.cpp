#include "app.h"
#include "updates.h"
#include "converter.h"
#include "options.h"
#include "progress.h"
#include "wheel.h"
#include <shellapi.h>
#include <fstream>
#include <string>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    int result = 0;
    const bool convert_command = argc >= 5 && std::wstring(argv[1]) == L"--convert" &&
        (argc == 5 || (argc == 6 && std::wstring(argv[5]) != L"--options") ||
        (argc == 7 && std::wstring(argv[5]) == L"--options") || (argc == 8 && std::wstring(argv[6]) == L"--options"));
    if (argc >= 2 && std::wstring(argv[1]) == L"--apply-update") { result = shiftwheel::apply_update(argc, argv); }
    else if (convert_command) {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) { LocalFree(argv); return 1; }
        try {
            const auto options = argc >= 7 ? shiftwheel::load_options(argv[argc - 1]) : shiftwheel::ConversionOptions{};
            const auto format = shiftwheel::parse_format(argc == 6 || argc == 8 ? argv[5] : std::filesystem::path(argv[3]).extension().wstring());
            shiftwheel::report_progress(-1, L"Подготовка");
            shiftwheel::convert_file(argv[2], argv[3], argv[4], format, options);
            shiftwheel::report_progress(100, L"Готово");
        }
        catch (const std::exception& error) {
            auto path = std::wstring(argv[4]) + L".error.txt";
            HANDLE log = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (log != INVALID_HANDLE_VALUE) {
                DWORD written = 0; const std::string message = error.what();
                WriteFile(log, message.data(), static_cast<DWORD>(message.size()), &written, nullptr); CloseHandle(log);
            }
            result = 1;
        }
        CoUninitialize();
    } else if ((argc == 3 || argc == 4) && std::wstring(argv[1]) == L"--render-preview") {
        HRESULT initialized = OleInitialize(nullptr);
        if (FAILED(initialized)) { LocalFree(argv); return 1; }
        Microsoft::WRL::ComPtr<shiftwheel::Wheel> wheel;
        wheel.Attach(new shiftwheel::Wheel([] { return false; }, [](std::filesystem::path, shiftwheel::Format) {}));
        HRESULT status = wheel->initialize(instance);
        if (SUCCEEDED(status)) status = wheel->render_preview(argv[2], argc == 4 ? argv[3] : L"preview.png");
        result = FAILED(status) ? 1 : 0;
        wheel->shutdown(); wheel.Reset(); OleUninitialize();
    } else if (argc == 1 || (argc == 2 && (std::wstring(argv[1]) == L"--preview" ||
        std::wstring(argv[1]) == L"--panel" || std::wstring(argv[1]) == L"--settings" || std::wstring(argv[1]) == L"--startup" || std::wstring(argv[1]) == L"--update-failed"))) {
        HRESULT initialized = OleInitialize(nullptr);
        if (FAILED(initialized)) { LocalFree(argv); return 1; }
        result = shiftwheel::run_app(instance, argc == 2 && std::wstring(argv[1]) == L"--preview",
            argc == 2 && std::wstring(argv[1]) == L"--panel", argc == 2 && (std::wstring(argv[1]) == L"--settings" || std::wstring(argv[1]) == L"--update-failed"), argc == 2 && std::wstring(argv[1]) == L"--startup", argc == 2 && std::wstring(argv[1]) == L"--update-failed"); OleUninitialize();
    } else result = 2;
    LocalFree(argv);
    return result;
}
