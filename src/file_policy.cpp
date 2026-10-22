#include "file_policy.h"
#include "diagnostics.h"
#include "policy.h"
#include <windows.h>
#include <stdexcept>
namespace shiftwheel {
void validate_local_path(const std::filesystem::path& path, bool require_file) {
    auto drive = path.root_path().wstring();
    if (drive.size() != 3 || drive[1] != L':' || path.relative_path().wstring().find(L':') != std::wstring::npos)
        throw std::runtime_error("Поддерживаются обычные локальные пути без потоков NTFS.");
    auto type = GetDriveTypeW(drive.c_str());
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) throw std::runtime_error("В прототипе доступны локальные диски.");
    auto current = path.root_path();
    for (const auto& part : path.relative_path()) {
        current /= part;
        auto attrs = GetFileAttributesW(current.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Ссылки и облачные файлы в прототипе не поддерживаются.");
    }
    if (require_file) {
        auto attrs = GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) throw std::runtime_error(file_error(GetLastError(),"Исходный файл недоступен."));
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) throw std::runtime_error("Выберите файл, а не папку.");
    }
}
bool supported_local_file(const std::filesystem::path& path) noexcept {
    try {
        if (!path.is_absolute() || !supported_input(path)) return false;
        validate_local_path(path.lexically_normal(), true);
        return true;
    } catch (...) { return false; }
}
}
