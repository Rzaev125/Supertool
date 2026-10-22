#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <string_view>
namespace shiftwheel {
std::string file_error(DWORD code, std::string_view context);
class DiagnosticLog {
public:
    static constexpr unsigned max_bytes = 256 * 1024;
    explicit DiagnosticLog(std::filesystem::path file) : file_(std::move(file)) {}
    void append(std::string_view event, unsigned long long id = 0) noexcept;
    void export_to(const std::filesystem::path& destination) const;
private:
    std::filesystem::path file_;
};
}
