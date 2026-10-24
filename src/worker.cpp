#include "worker.h"
#include "diagnostics.h"
#include "engines.h"
#include "file_policy.h"
#include <objbase.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace shiftwheel {
namespace fs = std::filesystem;
std::wstring from_utf8(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
Worker::~Worker() { shutdown(); }
JobProgress Worker::progress() const { std::lock_guard lock(progress_mutex_); return progress_; }
void Worker::start(const fs::path& input, Format format, HWND notify, const ConversionOptions& options, const fs::path& output_directory) {
    if (busy()) throw std::runtime_error("Дождитесь завершения текущей конвертации.");
    validate_local_path(input.lexically_normal(),true);
    if (!supported_local_file(input) || !supports_conversion(input, format)) throw std::runtime_error("Это преобразование недоступно.");
    validate_options(options);
    const auto destination = (output_directory.empty() ? input.parent_path() : output_directory).lexically_normal();
    if (!destination.is_absolute()) throw std::runtime_error("Папка результата должна иметь полный локальный путь.");
    validate_local_path(destination, false);
    const DWORD directory_attributes = GetFileAttributesW(destination.c_str());
    if (directory_attributes == INVALID_FILE_ATTRIBUTES || !(directory_attributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Выбранная папка результата недоступна.");
    require_conversion_engines(input, format);
    output_.clear();
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
        auto candidate = destination / output_candidate(input, format, suffix).filename();
        if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
                throw std::runtime_error("Не удалось проверить имя результата.");
            output_ = candidate; break;
        }
    }
    if (output_.empty()) throw std::runtime_error("Не удалось подобрать свободное имя.");
    GUID guid{}; if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Не удалось создать идентификатор задачи.");
    wchar_t token[40]{}; StringFromGUID2(guid, token, 40);
    // A fresh compact token leaves room for LibreOffice's private nested profile.
    // Atomic CreateDirectory still refuses every collision; no existing path is reused.
    std::wstring compact;for(auto c:std::wstring(token))if(iswxdigit(c))compact+=c;compact.resize(16);
    staging_ = destination / (std::wstring(L".shiftwheel-") + compact);
    if (!CreateDirectoryW(staging_.c_str(), nullptr)) { staging_.clear(); throw std::runtime_error(file_error(GetLastError(),"Не удалось создать временную папку результата.")); }
    SetFileAttributesW(staging_.c_str(), FILE_ATTRIBUTE_HIDDEN);
    temporary_ = staging_ / L"result.tmp";
    PROCESS_INFORMATION info{};
    try {
        const auto snapshot = staging_ / L"options.ini";
        save_options(snapshot, options);
        std::wstring executable(32768, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (!length || length >= executable.size()) throw std::runtime_error("Не удалось найти исполняемый файл.");
        executable.resize(length);
        std::wstring command = quote_argument(executable) + L" --convert " + quote_argument(input.wstring()) + L" " + quote_argument(output_.wstring()) + L" " + quote_argument(temporary_.wstring()) + L" " + quote_argument(extension(format)) + L" --options " + quote_argument(snapshot.wstring());
        if (command.size() >= 32767) throw std::runtime_error("Путь слишком длинный для запуска конвертера.");
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_) throw std::runtime_error("Не удалось создать изолированную задачу.");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Не удалось настроить завершение задачи.");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        WinHandle writer, input_null(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
        WinHandle error_null(CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
        if (input_null.value == INVALID_HANDLE_VALUE || error_null.value == INVALID_HANDLE_VALUE ||
            !CreatePipe(&progress_pipe_, &writer.value, &security, 0) || !SetHandleInformation(progress_pipe_, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("Не удалось подготовить канал прогресса.");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw std::runtime_error("Не удалось создать список дескрипторов.");
        struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Attributes() { DeleteProcThreadAttributeList(p); } } attribute_cleanup{attributes};
        HANDLE inherited[]{input_null.value, writer.value, error_null.value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
            throw std::runtime_error("Не удалось изолировать дескрипторы конвертера.");
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES; startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = input_null.value; startup.StartupInfo.hStdOutput = writer.value; startup.StartupInfo.hStdError = error_null.value;
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &info))
            throw std::runtime_error("Не удалось запустить конвертер.");
        process_ = info.hProcess;
        CloseHandle(writer.value); writer.value = INVALID_HANDLE_VALUE;
        if (!AssignProcessToJobObject(job_, process_)) throw std::runtime_error("Windows не разрешила изоляцию конвертера.");
        cancelled_ = false;
        { std::lock_guard lock(progress_mutex_); progress_ = {-1, L"Подготовка"}; facts_={}; }
        reader_ = std::thread([this, notify] {
            try {
                BoundedLines decoder;
                char buffer[4096]; DWORD received = 0;
                while (ReadFile(progress_pipe_, buffer, sizeof(buffer), &received, nullptr) && received) {
                    for (const auto& line : decoder.feed(std::string_view(buffer, received))) {
                        if(line.starts_with("SWE1\t")){std::lock_guard lock(progress_mutex_);parse_execution(line,facts_);continue;}
                        JobProgress event;
                        if (!parse_progress(line, event)) continue;
                        bool changed = false;
                        {
                            std::lock_guard lock(progress_mutex_);
                            changed = progress_.percent != event.percent || progress_.stage != event.stage;
                            if (changed) progress_ = std::move(event);
                        }
                        if (changed && notify) PostMessageW(notify, wm_worker_progress, 0, 0);
                    }
                }
            } catch (...) { /* Progress is advisory; conversion completion remains authoritative. */ }
        });
        started_tick_=GetTickCount64();
        if (ResumeThread(info.hThread) == static_cast<DWORD>(-1)) throw std::runtime_error("Не удалось запустить поток конвертера.");
        CloseHandle(info.hThread); info.hThread = nullptr;
        waiter_ = std::thread([this, notify] {
            WaitForSingleObject(process_, INFINITE);
            {std::lock_guard lock(progress_mutex_);facts_.elapsed_ms=GetTickCount64()-started_tick_;}
            // Descendants may still hold stdout open after the worker exits.
            stop_job(job_);
            if (reader_.joinable()) reader_.join();
            if (notify) PostMessageW(notify, wm_worker_done, 0, 0);
        });
    } catch (...) {
        if (info.hThread) CloseHandle(info.hThread);
        if (process_) { TerminateProcess(process_, ERROR_CANCELLED); WaitForSingleObject(process_, INFINITE); }
        cleanup();
        throw;
    }
}
void Worker::cancel() {
    if (process_) { cancelled_ = true; TerminateJobObject(job_, ERROR_CANCELLED); }
}
WorkerResult Worker::finish() {
    WorkerResult result; result.output = output_;
    if (!process_) return result;
    if (waiter_.joinable()) waiter_.join();
    DWORD code = 1; GetExitCodeProcess(process_, &code);
    // Only a successful child exit confirms publication. Cancellation never
    // deletes a final output, even if it races with the final rename.
    result.success = code == 0 && fs::exists(output_);
    result.cancelled = cancelled_ && !result.success;
    {std::lock_guard lock(progress_mutex_);result.facts=facts_;}
    if(!result.success)result.facts.output_bytes.reset();
    if (result.success) result.message = output_.filename().wstring();
    else if (result.cancelled) result.message = L"Конвертация отменена. Оригинал сохранён.";
    else {
        std::ifstream error(temporary_.wstring() + L".error.txt", std::ios::binary);
        std::string message{std::istreambuf_iterator<char>(error), {}};
        result.message = message.empty() ? L"Конвертер завершился с ошибкой. Оригинал сохранён." : from_utf8(message);
    }
    if(!result.success && !result.cancelled) {
        ULARGE_INTEGER free{};if(GetDiskFreeSpaceExW(output_.parent_path().c_str(),&free,nullptr,nullptr)&&free.QuadPart==0)
            result.message=from_utf8(file_error(ERROR_DISK_FULL,"Не удалось завершить конвертацию."));
    }
    cleanup(); return result;
}
void Worker::cleanup() {
    if (waiter_.joinable()) waiter_.join();
    if (job_) stop_job(job_);
    if (reader_.joinable()) reader_.join();
    if (progress_pipe_) { CloseHandle(progress_pipe_); progress_pipe_ = nullptr; }
    if (process_) { CloseHandle(process_); process_ = nullptr; }
    if (job_) { CloseHandle(job_); job_ = nullptr; }
    if (!staging_.empty()) {
        // Handle teardown/scanners can briefly retain engine.log after the job
        // has no active processes (ERROR_SHARING_VIOLATION). Retry only this
        // worker's private staging tree, with a bounded 100 ms total delay.
        for (int attempt = 0; attempt <= 5; ++attempt) {
            remove_owned_tree(staging_);
            if (GetFileAttributesW(staging_.c_str()) == INVALID_FILE_ATTRIBUTES) break;
            if (attempt < 5) Sleep(20);
        }
    }
    staging_.clear(); temporary_.clear();
}
void Worker::shutdown() {
    if (process_) {
        cancel();
        if (waiter_.joinable()) waiter_.join();
        else WaitForSingleObject(process_, INFINITE);
        cleanup();
    }
}
}
