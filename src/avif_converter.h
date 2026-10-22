#pragma once
#include "options.h"
namespace shiftwheel {
// All outputs here remain inside the caller's exclusively owned staging folder.
void convert_avif(const std::filesystem::path& input, const std::filesystem::path& output,
                  Format format, const std::filesystem::path& work, const ConversionOptions& options = {});
}
