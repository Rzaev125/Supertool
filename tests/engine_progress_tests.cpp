#include "engines.h"
#include "progress.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
namespace fs = std::filesystem;
using namespace shiftwheel;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static void write(HANDLE handle, const std::string& text) { DWORD count = 0; WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &count, nullptr); }
struct Capture {
    HANDLE read = nullptr, write = nullptr, old = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string text;
    std::thread reader;
    Capture() {
        check(CreatePipe(&read, &write, nullptr, 0) && SetStdHandle(STD_OUTPUT_HANDLE, write), "capture stdout");
        reader = std::thread([this] { char buffer[4096]; DWORD count = 0; while (ReadFile(read, buffer, sizeof(buffer), &count, nullptr) && count) text.append(buffer, count); });
    }
    ~Capture() { stop(); }
    void stop() { if (write) { SetStdHandle(STD_OUTPUT_HANDLE, old); CloseHandle(write); write = nullptr; reader.join(); CloseHandle(read); read = nullptr; } }
};
int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && std::wstring(argv[1]) == L"--hold") { Sleep(INFINITE); return 0; }
    if (argc >= 2 && std::wstring(argv[1]) == L"--copy") {
        write(GetStdHandle(STD_ERROR_HANDLE), "separate diagnostic\n");
        char buffer[4096]; DWORD count = 0;
        while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &count, nullptr) && count)
            write(GetStdHandle(STD_OUTPUT_HANDLE), std::string(buffer, count));
        return 0;
    }
    if (argc >= 2 && std::wstring(argv[1]) == L"--engine") {
        write(GetStdHandle(STD_ERROR_HANDLE), "controlled stderr\n");
        write(GetStdHandle(STD_OUTPUT_HANDLE), std::string(32768, 'x') + "\nout_time_us=250");
        write(GetStdHandle(STD_OUTPUT_HANDLE), "0000\nout_time_us=N/A\nout_time_us=10000000\n");
        std::wstring command = quote_argument(argv[0]) + L" --hold";
        STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION info{};
        if (!CreateProcessW(argv[0], command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info)) return 2;
        CloseHandle(info.hThread); CloseHandle(info.hProcess);
        if (argc == 3) Sleep(INFINITE);
        return 0;
    }
    auto root = fs::current_path() / "test-output" / (L"engine-progress-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    int result = 0;
    try {
        Capture capture;
        const auto start = GetTickCount64();
        run_engine(fs::absolute(argv[0]), {L"--engine"}, root / "success.log", 5000, 10, {}, {}, L"Конвертация: Intel QSV");
        capture.stop();
        check(GetTickCount64() - start < 5000, "exited engine descendant does not hold reader open");
        BoundedLines decoder; bool quarter = false, capped = false;
        for (const auto& line : decoder.feed(capture.text)) {
            JobProgress value; check(parse_progress(line, value), "engine emits only progress protocol");
            quarter |= value.percent == 25; capped |= value.percent == 99;
            check(value.percent < 100, "encoder must not report published completion");
            check(value.stage == L"Конвертация: Intel QSV", "engine progress retains the chosen encoder stage");
        }
        check(quarter && capped, "engine stdout progress reaches worker stream");
        std::ifstream log(root / "success.log"); std::string details{std::istreambuf_iterator<char>(log), {}};
        check(details == "controlled stderr\n", "stderr stays in log, stdout uses progress pipe");
        Capture timeout_capture;
        bool timed_out = false; const auto timeout_start = GetTickCount64();
        try { run_engine(fs::absolute(argv[0]), {L"--engine", L"wait"}, root / "timeout.log", 100, 10); }
        catch (const std::runtime_error&) { timed_out = true; }
        timeout_capture.stop();
        check(timed_out && GetTickCount64() - timeout_start < 5000, "timeout closes engine and descendant progress writers");
        Capture report_capture;
        report_progress(101, L"Invalid"); report_progress(-2, L"Invalid"); report_progress(2, std::wstring(1, wchar_t(0xd800)));
        report_progress(2, std::wstring(1000, L'Я'));
        report_capture.stop();
        auto events = decoder.feed(report_capture.text); JobProgress bounded;
        check(events.size() == 1 && parse_progress(events[0], bounded) && bounded.stage.size() == 160,
            "reporter drops invalid events and bounds Unicode stages");
        const auto unicode_root = root / L"Вход и çıxış";
        fs::create_directory(unicode_root);
        const auto source = unicode_root / L"Текст ə.txt", target = unicode_root / L"Результат ı.txt";
        constexpr char payload_bytes[] = "Unicode paths, binary input: \0\1\2";
        const std::string payload(payload_bytes, sizeof(payload_bytes) - 1);
        { std::ofstream stream(source, std::ios::binary); stream.write(payload.data(), payload.size()); }
        run_engine(fs::absolute(argv[0]), {L"--copy"}, unicode_root / L"Лог.log", 5000, 0, source, target);
        { std::ifstream stream(target, std::ios::binary); const std::string value{std::istreambuf_iterator<char>(stream), {}};
          check(value == payload, "Unicode stdin/stdout handles preserve binary contents"); }
        { std::ifstream stream(unicode_root / L"Лог.log", std::ios::binary); const std::string value{std::istreambuf_iterator<char>(stream), {}};
          check(value == "separate diagnostic\n", "file stdout excludes stderr diagnostics"); }
        bool refused = false;
        try { run_engine(fs::absolute(argv[0]), {L"--copy"}, root / "collision.log", 5000, 0, source, target); }
        catch (const std::runtime_error&) { refused = true; }
        check(refused, "file stdout never overwrites an existing target");
        { std::ifstream stream(target, std::ios::binary); const std::string value{std::istreambuf_iterator<char>(stream), {}};
          check(value == payload, "collision preserves original output"); }
        refused = false;
        try { run_engine(fs::absolute(argv[0]), {L"--copy"}, root / "conflicting.log", 5000, 1, source, root / "conflicting.txt"); }
        catch (const std::runtime_error&) { refused = true; }
        check(refused && !fs::exists(root / "conflicting.txt"), "progress stdout and file stdout are mutually exclusive");
        check(DeleteFileW(source.c_str()) && DeleteFileW(target.c_str()), "stdin/stdout handles close after calls and collision");
        std::cout << "PASS engine progress, Unicode file streams, separate stderr, collisions, descendant cleanup and timeout\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; result = 1; }
    remove_owned_tree(root); return result;
}
