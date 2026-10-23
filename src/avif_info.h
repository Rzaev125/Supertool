#pragma once
#include <filesystem>
#include "image_hdr.h"
namespace shiftwheel {
struct AvifImageInfo {
    bool profile = false;
    HdrTransfer hdr = HdrTransfer::None;
    int depth = 0, primaries = 0, transfer = 0, matrix = 0;
    bool operator==(const AvifImageInfo&) const = default;
};
AvifImageInfo inspect_decoded_avif(const std::filesystem::path& log);
// Validate pinned decoder diagnostics before publishing its normalized PNG.
bool validate_decoded_avif(const std::filesystem::path& log);
}
