#pragma once
#include "options.h"
#include <optional>
namespace shiftwheel {
struct AnimationInfo { unsigned width=0,height=0,plays=1;std::vector<unsigned> delays; };
bool animation_input(const std::filesystem::path& path);
AnimationInfo inspect_animation(const std::filesystem::path& path);
void convert_animation(const std::filesystem::path& input,const std::filesystem::path& output,Format format,const std::filesystem::path& work,const ConversionOptions& options);
}
