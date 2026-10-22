#pragma once
#include <filesystem>
namespace shiftwheel {
void validate_local_path(const std::filesystem::path& absolute_path, bool require_file);
bool supported_local_file(const std::filesystem::path& path) noexcept;
}
