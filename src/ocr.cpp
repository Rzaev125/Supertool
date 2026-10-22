#include "ocr.h"
#include "engines.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
std::vector<std::string> language_names(std::string_view languages) {
    std::vector<std::string> result;
    std::set<std::string> unique;
    while (!languages.empty()) {
        const auto separator = languages.find('+');
        const auto name = languages.substr(0, separator);
        if ((name != "eng" && name != "rus" && name != "aze") || !unique.emplace(name).second)
            throw std::runtime_error("Некорректный набор языков OCR.");
        result.emplace_back(name);
        if (separator == std::string_view::npos) break;
        languages.remove_prefix(separator + 1);
        if (languages.empty()) throw std::runtime_error("Некорректный набор языков OCR.");
    }
    if (result.empty()) throw std::runtime_error("Выберите хотя бы один язык OCR.");
    return result;
}
bool plain_file(const fs::path& file) {
    const DWORD attributes = GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
}
fs::path ocr_module() { return module_path(Category::Pdf).parent_path().parent_path() / L"ocr" / L"tesseract.exe"; }
void require_ocr(const std::string& languages) {
    auto names = language_names(languages);
    // The official Latin Azerbaijani model also requests its Cyrillic companion.
    if (std::find(names.begin(), names.end(), "aze") != names.end()) names.emplace_back("aze_cyrl");
    const auto executable = ocr_module();
    if (!plain_file(executable)) throw std::runtime_error("Для сканированных страниц нужен модуль OCR. Верните папку modules/ocr рядом с ShiftWheel.exe.");
    for (const auto& language : names) {
        const auto model = executable.parent_path() / L"tessdata" / (language + ".traineddata");
        if (!plain_file(model)) throw std::runtime_error("В модуле OCR отсутствует языковая модель " + language + ". Верните папку modules/ocr/tessdata.");
    }
}
std::wstring recognize_page(const fs::path& png, const fs::path& work, const std::string& languages) {
    require_ocr(languages);
    const auto executable = ocr_module();
    const auto result = work / L"recognized.txt";
    if (fs::exists(result)) throw std::runtime_error("Временный результат OCR уже существует.");
    // Unicode-safe Windows handles avoid Tesseract's narrow filename APIs. Only
    // our rendered PNG reaches stdin; cwd is the module, so tessdata stays ASCII.
    run_engine(executable, {L"stdin", L"stdout", L"--tessdata-dir", L"tessdata",
        L"-l", std::wstring(languages.begin(), languages.end()), L"--oem", L"1", L"--psm", L"3", L"--dpi", L"300"},
        work / L"ocr.log", 120000, 0, png, result);
    if (!plain_file(result)) throw std::runtime_error("OCR не создал текст страницы.");
    const auto bytes = fs::file_size(result);
    if (bytes > 16 * 1024 * 1024) throw std::runtime_error("Слишком большой текст страницы OCR.");
    std::ifstream input(result, std::ios::binary);
    std::string text(static_cast<size_t>(bytes), '\0');
    if (!input || (!text.empty() && !input.read(text.data(), static_cast<std::streamsize>(text.size()))))
        throw std::runtime_error("Не удалось прочитать результат OCR.");
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) throw std::runtime_error("OCR вернул некорректный текст UTF-8.");
    std::wstring decoded(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), decoded.data(), count) != count)
        throw std::runtime_error("Не удалось прочитать текст OCR.");
    return decoded;
}
}
