#pragma once
#include <filesystem>
namespace shiftwheel {
// Validate pinned decoder diagnostics before publishing its normalized PNG.
bool validate_decoded_avif(const std::filesystem::path& log);
}
