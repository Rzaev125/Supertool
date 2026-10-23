#include "engines.h"
#include "module_paths.h"
#include "file_policy.h"
#include "progress.h"
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <thread>

namespace shiftwheel {
namespace fs = std::filesystem;
std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Некорректный текст Unicode.");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
fs::path module_path(Category category) {
    switch (category) {
    case Category::Video: case Category::Audio: return module_directory("ffmpeg") / L"ffmpeg.exe";
    case Category::Document: case Category::Spreadsheet: case Category::Presentation: return module_directory("libreoffice") / L"program" / L"soffice.com";
    case Category::Pdf: return module_directory("pdfium") / L"pdfium.dll";
    default: return {};
    }
}
void require_engine(Category category) {
    auto path = module_path(category);
    if (!path.empty() && !fs::is_regular_file(path))
        throw std::runtime_error("Модуль конвертации отсутствует. Откройте Настройки → Модули и установите нужный компонент.");
}
fs::path avif_module(bool encode) { return module_directory("avif") / (encode ? L"avifenc.exe" : L"avifdec.exe"); }
void require_conversion_engines(const fs::path& input, Format format) {
    require_engine(category(input));
    if (category(input) == Category::Presentation && (format == Format::Png || format == Format::Jpeg)) require_engine(Category::Pdf);
    if (category(input) != Category::Image) return;
    if (format == Format::Webp || parse_format(input.extension().wstring()) == Format::Webp) require_engine(Category::Video);
    for (bool encode : {false, true}) {
        const bool needed = encode ? format == Format::Avif : parse_format(input.extension().wstring()) == Format::Avif;
        if (needed && !fs::is_regular_file(avif_module(encode)))
            throw std::runtime_error("Модуль AVIF отсутствует. Откройте Настройки → Модули → AVIF.");
    }
    if (format == Format::Heic || parse_format(input.extension().wstring()) == Format::Heic) {
        const auto root = module_directory("heic");
        if (!fs::is_regular_file(root / L"libheif.dll") ||
            (format == Format::Heic && !fs::is_regular_file(root / L"heif-enc.exe")) ||
            (parse_format(input.extension().wstring()) == Format::Heic && !fs::is_regular_file(root / L"heif-dec.exe")))
            throw std::runtime_error("Модуль HEIC отсутствует. Откройте Настройки → Модули → HEIC.");
    }
}
void stop_job(HANDLE job) noexcept {
    if (!job) return;
    TerminateJobObject(job, ERROR_CANCELLED);
    // Job completion is asynchronous; wait before attempting to remove open files.
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) || !accounting.ActiveProcesses) return;
        Sleep(10);
    }
}
void remove_owned_tree(const fs::path& root) noexcept {
    try {
        auto absolute = fs::absolute(root).lexically_normal();
        if (absolute == absolute.root_path() || absolute.parent_path().empty()) return;
        validate_local_path(absolute.parent_path(), false);
        auto attrs = GetFileAttributesW(absolute.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return;
        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
            if (attrs & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(absolute.c_str()); else DeleteFileW(absolute.c_str());
            return;
        }
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            for (const auto& entry : fs::directory_iterator(absolute)) remove_owned_tree(entry.path());
            RemoveDirectoryW(absolute.c_str());
        } else { SetFileAttributesW(absolute.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(absolute.c_str()); }
    } catch (...) { /* Cleanup must never hide the conversion's original error. */ }
}
void run_engine(const fs::path& executable, const std::vector<std::wstring>& arguments, const fs::path& log, DWORD timeout, double duration_seconds,
                const fs::path& standard_input, const fs::path& standard_output, const std::wstring& progress_stage) {
    const bool progress_enabled = std::isfinite(duration_seconds) && duration_seconds > 0;
    if (duration_seconds > 0 && !standard_output.empty())
        throw std::runtime_error("Поток результата модуля уже используется для прогресса.");
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote_argument(argument);
    if (command.size() >= 32767) throw std::runtime_error("Слишком длинный путь или команда.");
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    WinHandle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    WinHandle input(CreateFileW(standard_input.empty() ? L"NUL" : standard_input.c_str(), GENERIC_READ,
        standard_input.empty() ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, &security, OPEN_EXISTING, 0, nullptr));
    WinHandle result;
    if (!standard_output.empty()) {
        result.value = CreateFileW(standard_output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (result.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Не удалось создать файл результата модуля. Файл уже существует или путь недоступен.");
    }
    WinHandle job(CreateJobObjectW(nullptr, nullptr));
    if (output.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE || !job.value) throw std::runtime_error("Не удалось подготовить запуск модуля.");
    WinHandle progress_read, progress_write;
    if (progress_enabled && (!CreatePipe(&progress_read.value, &progress_write.value, &security, 0) ||
        !SetHandleInformation(progress_read.value, HANDLE_FLAG_INHERIT, 0))) throw std::runtime_error("Не удалось подготовить прогресс модуля.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Не удалось ограничить время жизни модуля.");
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw std::runtime_error("Не удалось создать список дескрипторов.");
    struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes() { DeleteProcThreadAttributeList(p); } } cleanup{attributes};
    HANDLE inherited[]{input.value, output.value, progress_enabled ? progress_write.value : result.value};
    const SIZE_T inherited_bytes = sizeof(HANDLE) * (progress_enabled || !standard_output.empty() ? 3 : 2);
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, inherited_bytes, nullptr, nullptr)) throw std::runtime_error("Не удалось изолировать дескрипторы.");
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdError = output.value;
    startup.StartupInfo.hStdOutput = progress_enabled ? progress_write.value : (standard_output.empty() ? output.value : result.value);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT,
        nullptr, executable.parent_path().c_str(), &startup.StartupInfo, &info)) throw std::runtime_error("Не удалось запустить модуль конвертации.");
    WinHandle process(info.hProcess), thread(info.hThread);
    if (progress_enabled) { CloseHandle(progress_write.value); progress_write.value = INVALID_HANDLE_VALUE; }
    std::thread reader;
    DWORD wait = WAIT_FAILED, code = 1;
    try {
        if (!AssignProcessToJobObject(job.value, process.value))
            throw std::runtime_error("Windows не разрешила запуск изолированного модуля.");
        if (progress_enabled) reader = std::thread([pipe = progress_read.value, duration_seconds, progress_stage] {
            try {
                BoundedLines decoder;
                char buffer[4096]; DWORD received = 0;
                int previous = -1;
                while (ReadFile(pipe, buffer, sizeof(buffer), &received, nullptr) && received) {
                    for (const auto& line : decoder.feed(std::string_view(buffer, received))) {
                        int percent = -1;
                        if (parse_engine_progress(line, duration_seconds, percent) && percent > previous) {
                            report_progress(percent, progress_stage); previous = percent;
                        }
                    }
                }
            } catch (...) { /* A missing progress update must not abort an otherwise valid conversion. */ }
        });
        if (ResumeThread(thread.value) == DWORD(-1)) throw std::runtime_error("Не удалось запустить поток модуля.");
        wait = WaitForSingleObject(process.value, timeout);
        if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.value, &code);
        // Kill every descendant before joining: a helper can inherit stdout and
        // keep it open after the main encoder exits or times out.
        stop_job(job.value);
        if (reader.joinable()) reader.join();
    } catch (...) {
        TerminateProcess(process.value, ERROR_CANCELLED);
        stop_job(job.value);
        WaitForSingleObject(process.value, INFINITE);
        if (reader.joinable()) reader.join();
        throw;
    }
    if (wait != WAIT_OBJECT_0) throw std::runtime_error("Превышено время ожидания конвертации.");
    if (code == ERROR_CANCELLED) throw EngineCancelled();
    if (code != 0) {
        std::ifstream file(log, std::ios::binary); std::string details(4096, '\0'); file.read(details.data(), details.size()); details.resize(static_cast<size_t>(file.gcount()));
        throw std::runtime_error("Модуль не смог преобразовать файл. " + details);
    }
}
}
