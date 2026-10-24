#include "progress.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <mutex>
namespace shiftwheel {
namespace {
constexpr std::size_t stage_limit = 160;
bool valid_percent(int value) { return value >= -1 && value <= 100; }
bool control(wchar_t value) { return value < L' ' || (value >= 0x7f && value <= 0x9f); }
}
std::vector<std::string> BoundedLines::feed(std::string_view bytes) {
    std::vector<std::string> lines;
    for (char byte : bytes) {
        if (byte == '\n') {
            if (!discarding_) {
                if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
                lines.push_back(std::move(pending_));
            }
            pending_.clear(); discarding_ = false;
        } else if (!discarding_) {
            if (pending_.size() == progress_line_limit) { pending_.clear(); discarding_ = true; }
            else pending_ += byte;
        }
    }
    return lines;
}
bool parse_progress(std::string_view line, JobProgress& result) {
    if (line.size() > progress_line_limit || !line.starts_with("SWP1\t")) return false;
    line.remove_prefix(5);
    auto tab = line.find('\t');
    if (tab == std::string_view::npos) return false;
    int percent = -2;
    auto number = line.substr(0, tab);
    auto parsed = std::from_chars(number.data(), number.data() + number.size(), percent);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !valid_percent(percent)) return false;
    auto stage = line.substr(tab + 1);
    if (stage.empty()) return false;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, stage.data(), static_cast<int>(stage.size()), nullptr, 0);
    if (count <= 0 || count > static_cast<int>(stage_limit)) return false;
    std::wstring decoded(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, stage.data(), static_cast<int>(stage.size()), decoded.data(), count) != count ||
        std::any_of(decoded.begin(), decoded.end(), control)) return false;
    result = {percent, std::move(decoded)};
    return true;
}
bool parse_engine_progress(std::string_view line, double duration, int& percent) {
    if (!std::isfinite(duration) || duration <= 0 || !line.starts_with("out_time_us=")) return false;
    line.remove_prefix(12);
    unsigned long long micros = 0;
    auto parsed = std::from_chars(line.data(), line.data() + line.size(), micros);
    if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size()) return false;
    const long double ratio = static_cast<long double>(micros) / static_cast<long double>(duration) / 10000.0L;
    percent = ratio >= 99 ? 99 : static_cast<int>(ratio);
    return true;
}
void report_progress(int percent, const std::wstring& stage) {
    if (!valid_percent(percent) || stage.empty()) return;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!output || output == INVALID_HANDLE_VALUE || GetFileType(output) != FILE_TYPE_PIPE) return;
    std::wstring clean = stage.substr(0, stage_limit);
    if (!clean.empty() && clean.back() >= 0xd800 && clean.back() <= 0xdbff) clean.pop_back();
    for (auto& character : clean) if (control(character)) character = L' ';
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, clean.data(), static_cast<int>(clean.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return;
    std::string encoded(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, clean.data(), static_cast<int>(clean.size()), encoded.data(), count, nullptr, nullptr) != count) return;
    const std::string event = "SWP1\t" + std::to_string(percent) + "\t" + encoded + "\n";
    if (event.size() > progress_line_limit) return;
    write_worker_event(event);
}
void write_worker_event(std::string_view event) {
    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    if(!output||output==INVALID_HANDLE_VALUE||GetFileType(output)!=FILE_TYPE_PIPE||event.size()>progress_line_limit)return;
    static std::mutex write_mutex;
    std::lock_guard lock(write_mutex);
    DWORD written = 0;
    WriteFile(output, event.data(), static_cast<DWORD>(event.size()), &written, nullptr);
}
}
