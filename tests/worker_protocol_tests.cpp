#include "worker.h"
#include "engines.h"
#include <objbase.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace fs = std::filesystem;
using namespace shiftwheel;
static bool done = false, progress_notice = false, payload_error = false;
static LRESULT CALLBACK notify_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == wm_worker_done || message == wm_worker_progress) {
        payload_error |= wparam != 0 || lparam != 0;
        done |= message == wm_worker_done; progress_notice |= message == wm_worker_progress;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static void pump() { MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message); }
template<class Predicate> static void until(Predicate predicate, const char* reason) {
    const auto started = GetTickCount64();
    while (!predicate() && GetTickCount64() - started < 5000) { pump(); Sleep(5); }
    check(predicate(), reason);
}
static void write(const std::string& text) { DWORD count = 0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &count, nullptr); }
int wmain(int argc, wchar_t** argv) {
    if (argc == 8 && std::wstring(argv[1]) == L"--convert") {
        if (std::wstring(argv[6]) != L"--options") return 2;
        auto input = fs::path(argv[2]);
        report_progress(42, L"Кодирование\nкадров\t");
        write("SWP1\t101\tInvalid\n" + std::string(32768, 'x') + "SWP1\t90\tInjected\n");
        if (input.stem() == L"cancel") { Sleep(INFINITE); return 0; }
        auto gate = input.parent_path() / L"continue";
        for (int i = 0; i < 500 && !fs::exists(gate); ++i) Sleep(10);
        if (!fs::exists(gate)) return 3;
        try {
            auto options = load_options(argv[7]);
            std::ofstream result(argv[3]); result << static_cast<int>(options.quality) << ' ' << options.image_max_dimension; result.close();
            if (!result) return 4;
            write("SWP1\t1"); write("00\tГотово\n");
        } catch (...) { return 5; }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto root = fs::current_path() / L"test-output" / (L"worker-protocol-" + std::to_wstring(GetTickCount64()));
    const auto destination = root / L"Результаты";
    fs::create_directories(destination);
    auto input = root / L"complete.png"; { std::ofstream file(input); file << "original"; }
    { std::ofstream file(destination / L"complete.jpg"); file << "existing"; }
    WNDCLASSW window_class{}; window_class.lpfnWndProc = notify_window; window_class.hInstance = GetModuleHandleW(nullptr); window_class.lpszClassName = L"ShiftWheel.ProtocolTest";
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(0, window_class.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const auto lock_path = root / L"parent-only.lock";
    WinHandle secret(CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Worker worker; int result = 0;
    try {
        check(window != nullptr && secret.value != INVALID_HANDLE_VALUE, "create test window and inheritable file lock");
        ConversionOptions options; options.quality = QualityPreset::High; options.image_max_dimension = 640;
        worker.start(input, Format::Jpeg, window, options, destination);
        options.quality = QualityPreset::Compact; options.image_max_dimension = 128;
        until([&] { return progress_notice && worker.progress().percent == 42; }, "initial progress notification did not arrive");
        check(worker.progress().stage == L"Кодирование кадров ", "child sanitizes line-breaking stage characters");
        CloseHandle(secret.value); secret.value = INVALID_HANDLE_VALUE;
        {
            WinHandle reopened(CreateFileW(lock_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            check(reopened.value != INVALID_HANDLE_VALUE, "unrelated inheritable parent file lock is excluded");
        }
        fs::path staging;
        for (const auto& entry : fs::directory_iterator(destination)) if (entry.is_directory()) staging = entry.path();
        check(!staging.empty() && fs::is_regular_file(staging / L"options.ini"), "snapshot belongs to output staging");
        { std::ofstream gate(root / L"continue"); gate << "continue"; }
        until([] { return done; }, "completion notification did not arrive");
        auto outcome = worker.finish();
        check(outcome.success && outcome.output == destination / L"complete (1).jpg", "configured output directory preserves collision naming");
        std::ifstream published(outcome.output); std::string content{std::istreambuf_iterator<char>(published), {}};
        check(content == "2 640", "child receives immutable options snapshot");
        check(worker.progress().percent == 100 && !payload_error, "fragmented completion updates snapshot without message pointers");
        check(!fs::exists(staging), "completed options snapshot and staging removed");
        check(fs::exists(input), "original retained");
        auto cancelled_input = root / L"cancel.png"; { std::ofstream file(cancelled_input); file << "original"; }
        DWORD baseline = 0; GetProcessHandleCount(GetCurrentProcess(), &baseline);
        for (int repeat = 0; repeat < 6; ++repeat) {
            done = false; progress_notice = false;
            worker.start(cancelled_input, Format::Jpeg, window, {}, destination);
            until([&] { return worker.progress().percent == 42; }, "restarted worker progress did not arrive");
            if (repeat % 2 == 0) { worker.cancel(); const auto cancelled = worker.finish(); check(cancelled.cancelled && !cancelled.success, "cancel closes progress reader"); }
            else worker.shutdown();
            check(!worker.busy(), "cancel and shutdown restore idle state");
        }
        DWORD after = 0; GetProcessHandleCount(GetCurrentProcess(), &after);
        check(after <= baseline + 1, "repeated cancel and shutdown leak no handles");
        for (const auto& entry : fs::directory_iterator(destination)) check(!entry.is_directory(), "no staging remains after cancellation");
        bool rejected = false;
        try { worker.start(input, Format::Jpeg, window, {}, fs::path(L"relative-output")); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected && !worker.busy(), "relative output directory is rejected before launch");
        std::cout << "PASS worker snapshot, output directory, progress notifications, handle allowlist and repeated cancellation cleanup\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << " (progress=" << worker.progress().percent << ", notice=" << progress_notice << ", done=" << done << ")\n"; result = 1; }
    worker.shutdown();
    if (secret.value != INVALID_HANDLE_VALUE) { CloseHandle(secret.value); secret.value = INVALID_HANDLE_VALUE; }
    DestroyWindow(window); remove_owned_tree(root); CoUninitialize(); return result;
}
