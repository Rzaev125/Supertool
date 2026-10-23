#pragma once
#include "options.h"
namespace shiftwheel {
struct PhotoExif;
enum class ImageColor { Unmanaged, ToSrgb, RequireProfile };
void convert_image(const std::filesystem::path& input, const std::filesystem::path& output,
                   const std::filesystem::path& temporary, ImageColor color = ImageColor::Unmanaged,
                   const ConversionOptions& options = {}, unsigned orientation_override = 0, const PhotoExif* metadata_override = nullptr);
}
