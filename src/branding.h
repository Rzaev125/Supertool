#pragma once
#include <filesystem>

namespace shiftwheel {
// Existing portable installations keep their state in place. Prefer the new
// name when both files exist; never overwrite or merge the user's old files.
inline std::filesystem::path application_state_path(const std::filesystem::path& directory, const wchar_t* suffix) {
    const auto current = directory / (std::wstring(L"Supertool") + suffix);
    if (std::filesystem::symlink_status(current).type() != std::filesystem::file_type::not_found) return current;
    const auto legacy = directory / (std::wstring(L"ShiftWheel") + suffix);
    return std::filesystem::symlink_status(legacy).type() != std::filesystem::file_type::not_found ? legacy : current;
}
}
