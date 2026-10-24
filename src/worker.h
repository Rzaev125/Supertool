#pragma once
#include "policy.h"
#include "options.h"
#include "progress.h"
#include "execution.h"
#include <windows.h>
#include <mutex>
#include <thread>
namespace shiftwheel {
inline constexpr UINT wm_worker_done = WM_APP + 20;
inline constexpr UINT wm_worker_progress = WM_APP + 21;
struct WorkerResult {
    bool success = false;
    bool cancelled = false;
    std::wstring message;
    std::filesystem::path output;
    ExecutionFacts facts;
};
class Worker {
public:
    ~Worker();
    bool busy() const { return process_ != nullptr; }
    void start(const std::filesystem::path& input, Format format, HWND notify,
               const ConversionOptions& options = {}, const std::filesystem::path& output_directory = {});
    JobProgress progress() const;
    void cancel();
    WorkerResult finish();
    void shutdown();
private:
    void cleanup();
    HANDLE process_ = nullptr, job_ = nullptr, progress_pipe_ = nullptr;
    std::thread waiter_, reader_;
    mutable std::mutex progress_mutex_;
    JobProgress progress_;
    ExecutionFacts facts_;
    ULONGLONG started_tick_ = 0;
    bool cancelled_ = false;
    std::filesystem::path output_, staging_, temporary_;
};
std::wstring from_utf8(const std::string& value);
}
