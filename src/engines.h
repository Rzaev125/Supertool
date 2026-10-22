#pragma once
#include "policy.h"
#include <windows.h>
#include <stdexcept>
#include <vector>

namespace shiftwheel {
struct WinHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit WinHandle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~WinHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
};
std::filesystem::path module_path(Category category);
void require_engine(Category category);
std::filesystem::path avif_module(bool encode);
void require_conversion_engines(const std::filesystem::path& input, Format format);
class EngineCancelled : public std::runtime_error {
public:
    EngineCancelled() : std::runtime_error("Конвертация отменена.") {}
};
void run_engine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
                const std::filesystem::path& log, DWORD timeout_ms, double duration_seconds = 0,
                const std::filesystem::path& standard_input = {}, const std::filesystem::path& standard_output = {},
                const std::wstring& progress_stage = L"Конвертация");
void stop_job(HANDLE job) noexcept;
// Call only for a directory exclusively created by this job. Never follows links.
void remove_owned_tree(const std::filesystem::path& root) noexcept;
std::string utf8(std::wstring_view value);
}
