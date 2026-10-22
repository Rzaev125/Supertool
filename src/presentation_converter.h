#pragma once
#include "options.h"
namespace shiftwheel {
// Output and work are exclusively owned staging paths, never user destinations.
void convert_presentation(const std::filesystem::path& input, const std::filesystem::path& output,
                          Format format, const std::filesystem::path& work,
                          const ConversionOptions& options = {});
}
