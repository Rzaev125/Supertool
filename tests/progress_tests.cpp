#include "progress.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace shiftwheel;
static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        JobProgress value;
        check(parse_progress("SWP1\t37\tEncoding", value) && value.percent == 37 && value.stage == L"Encoding", "valid progress is parsed");
        check(parse_progress("SWP1\t-1\tПодготовка", value) && value.percent == -1 && value.stage == L"Подготовка", "UTF-8 indeterminate event is parsed");
        check(parse_progress("SWP1\t100\tDone", value) && value.percent == 100, "published completion is accepted");
        for (const auto& invalid : {"SWP1\t101\tInvalid", "SWP1\t-2\tInvalid", "SWP1\t+2\tInvalid", "SWP1\t1x\tInvalid", "SWP1\t\tInvalid", "SWP1\t2\t", "SWP1\t2\tBad\tStage", "SWP1\t2\tBad\rStage", "noise", "SWP1\t2\t\xC0\xAF"})
            check(!parse_progress(invalid, value), "invalid event is rejected");
        check(!parse_progress(std::string("SWP1\t2\t") + std::string(2000, 'x'), value), "oversized stage is rejected");
        BoundedLines decoder;
        check(decoder.feed("SWP1\t3").empty(), "partial event is buffered");
        auto lines = decoder.feed("7\tEncoding\r\nSWP1\t-1\tPrepare\n");
        check(lines.size() == 2 && lines[0] == "SWP1\t37\tEncoding" && lines[1] == "SWP1\t-1\tPrepare", "fragments and CRLF produce separate events");
        check(decoder.feed(std::string(progress_line_limit * 20, 'x')).empty(), "oversized fragment is discarded");
        lines = decoder.feed("SWP1\t90\tInjected\nSWP1\t40\tReal\n");
        check(lines.size() == 1 && lines[0] == "SWP1\t40\tReal", "oversized line cannot inject a suffix event");
        int percent = -1;
        check(parse_engine_progress("out_time_us=2500000", 10, percent) && percent == 25, "FFmpeg microseconds become percentage");
        check(parse_engine_progress("out_time_us=20000000", 10, percent) && percent == 99, "encoder completion stays below publication completion");
        check(parse_engine_progress("out_time_us=0", 10, percent) && percent == 0, "zero time is valid");
        for (const auto& invalid : {"out_time_us=-1", "out_time_us=N/A", "out_time_us=2x", "out_time_us=", "out_time_us=18446744073709551616", "out_time_ms=1"})
            check(!parse_engine_progress(invalid, 10, percent), "malformed engine progress is rejected");
        for (double duration : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
            check(!parse_engine_progress("out_time_us=1", duration, percent), "invalid duration is rejected");
        std::cout << "PASS progress framing, strict events, UTF-8, oversized lines and FFmpeg percentages\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
