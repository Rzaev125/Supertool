#pragma once
#include <filesystem>
#include <string>

namespace shiftwheel {
std::filesystem::path ocr_module();
void require_ocr(const std::string& languages);
// work is an exclusively owned page directory. Text is UTF-8 decoded strictly.
std::wstring recognize_page(const std::filesystem::path& png,
                            const std::filesystem::path& work,
                            const std::string& languages);
}
