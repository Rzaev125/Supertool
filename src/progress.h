#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace shiftwheel {
struct JobProgress { int percent = -1; std::wstring stage; };
inline constexpr std::size_t progress_line_limit = 1024;
class BoundedLines {
public:
    std::vector<std::string> feed(std::string_view bytes);
private:
    std::string pending_;
    bool discarding_ = false;
};
bool parse_progress(std::string_view line, JobProgress& result);
bool parse_engine_progress(std::string_view line, double duration_seconds, int& percent);
void report_progress(int percent, const std::wstring& stage);
void write_worker_event(std::string_view event);
}
