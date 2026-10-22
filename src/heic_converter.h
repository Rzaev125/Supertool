#pragma once
#include "policy.h"
#include "options.h"
#include <filesystem>

namespace shiftwheel {
std::filesystem::path heic_module(const wchar_t* filename);
void convert_heic(const std::filesystem::path& input, const std::filesystem::path& output,
                  Format format, const std::filesystem::path& work, const ConversionOptions& options);
}
