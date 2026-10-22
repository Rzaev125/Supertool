#pragma once
#include "options.h"
namespace shiftwheel {
void convert_file(const std::filesystem::path& input, const std::filesystem::path& output,
                  const std::filesystem::path& temporary, Format format, const ConversionOptions& options = {});
}
