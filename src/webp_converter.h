#pragma once
#include "options.h"
namespace shiftwheel {
void convert_webp(const std::filesystem::path& input, const std::filesystem::path& output,
                  Format format, const std::filesystem::path& work, const ConversionOptions& options = {});
}
