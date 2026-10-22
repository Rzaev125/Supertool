#pragma once
#include "options.h"
#include <functional>
#include <vector>

namespace shiftwheel {
// A media operation describes the process boundary separately from conversion
// policy and file ownership. The default runner uses the isolated engine job.
struct MediaEngineCall {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::filesystem::path log;
    unsigned long timeout_ms = 0;
    double duration_seconds = 0;
    std::wstring progress_stage = L"Конвертация";
};
using MediaRunner = std::function<void(const MediaEngineCall&)>;
void convert_media(const std::filesystem::path& input, const std::filesystem::path& output,
                   Format format, const std::filesystem::path& work, const ConversionOptions& options,
                   const MediaRunner& runner = {});
}
