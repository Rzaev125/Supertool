#include "options.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <set>
#include <stdexcept>
#include <thread>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
constexpr DWORD max_config_size = 65536;
[[noreturn]] void invalid() { throw std::runtime_error("Некорректные настройки ShiftWheel."); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) invalid();
    std::wstring result(n, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n)) invalid();
    return result;
}
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) invalid();
    std::string result(n, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr)) invalid();
    return result;
}
const wchar_t* category_example(Category category) {
    switch (category) {
    case Category::Image: return L"x.png"; case Category::Video: return L"x.mp4";
    case Category::Audio: return L"x.wav"; case Category::Document: return L"x.docx";
    case Category::Spreadsheet: return L"x.xlsx"; case Category::Pdf: return L"x.pdf";
    case Category::Presentation: return L"x.pptx";
    default: invalid();
    }
}
const char* category_key(Category category) {
    switch (category) {
    case Category::Image: return "image"; case Category::Video: return "video";
    case Category::Audio: return "audio"; case Category::Document: return "document";
    case Category::Spreadsheet: return "spreadsheet"; case Category::Pdf: return "pdf";
    case Category::Presentation: return "presentation";
    default: invalid();
    }
}
void validate_settings(const Settings& settings) {
    validate_options(settings.conversion);
    if (settings.notifications != NotificationMode::All && settings.notifications != NotificationMode::Errors && settings.notifications != NotificationMode::None) invalid();
    if (settings.update_channel.size() > 4096 || settings.update_channel.find_first_of(L"\r\n\t ") != std::wstring::npos || (!settings.update_channel.empty() && !settings.update_channel.starts_with(L"https://"))) invalid();
    auto path = settings.output_directory.wstring();
    if (path.size() > 16000 || path.find_first_of(std::wstring_view(L"\r\n\0", 3)) != std::wstring::npos) invalid();
    if (narrow(path).size() + std::string_view("output_directory=").size() > 32768) invalid();
    for (const auto& [category, formats] : settings.wheels) {
        const auto supported = formats_for(category_example(category));
        if (formats.empty() || formats.size() > supported.size()) invalid();
        std::set<Format> unique;
        for (auto format : formats)
            if (std::find(supported.begin(), supported.end(), format) == supported.end() || !unique.insert(format).second) invalid();
    }
}
unsigned number(std::string_view value) {
    unsigned result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) invalid();
    return result;
}
bool conversion_field(ConversionOptions& o, std::string_view key, std::string_view value) {
    if (key == "quality") {
        if (value == "compact") o.quality = QualityPreset::Compact;
        else if (value == "balanced") o.quality = QualityPreset::Balanced;
        else if (value == "high") o.quality = QualityPreset::High; else invalid();
    } else if (key == "resources") {
        if (value == "economy") o.resources = ResourceMode::Economy;
        else if (value == "balanced") o.resources = ResourceMode::Balanced;
        else if (value == "fast") o.resources = ResourceMode::Fast; else invalid();
    } else if (key == "image_max_dimension") o.image_max_dimension = number(value);
    else if (key == "video_max_height") o.video_max_height = number(value);
    else if (key == "audio_bitrate_kbps") o.audio_bitrate_kbps = number(value);
    else if (key == "pdf_dpi") o.pdf_dpi = number(value);
    else if (key == "pdf_ocr") { if (value != "0" && value != "1") invalid(); o.pdf_ocr = value == "1"; }
    else if (key == "ocr_languages") o.ocr_languages = value;
    else if (key == "image_metadata") {
        if (value == "remove") o.image_metadata = ImageMetadata::Remove;
        else if (value == "capture_date") o.image_metadata = ImageMetadata::CaptureDate;
        else if (value == "photo_details") o.image_metadata = ImageMetadata::PhotoDetails;
        else invalid();
    }
    else if (key == "video_codec") {
        if (value == "default") o.video_codec = VideoCodec::Default;
        else if (value == "h264") o.video_codec = VideoCodec::H264;
        else if (value == "hevc") o.video_codec = VideoCodec::Hevc;
        else if (value == "av1") o.video_codec = VideoCodec::Av1;
        else invalid();
    }
    else if (key == "video_acceleration") {
        if (value == "auto") o.video_acceleration = VideoAcceleration::Auto;
        else if (value == "cpu") o.video_acceleration = VideoAcceleration::Cpu;
        else invalid();
    }
    else return false;
    return true;
}
Settings read_config(const fs::path& path, bool options_only, bool missing_allowed) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (missing_allowed && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) return {};
        throw std::runtime_error("Не удалось прочитать настройки ShiftWheel.");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.value, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nFileSizeHigh || info.nFileSizeLow > max_config_size) invalid();
    std::string data(info.nFileSizeLow, '\0'); DWORD count = 0;
    if (!ReadFile(file.value, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) || count != data.size()) invalid();
    if (data.find('\0') != std::string::npos) invalid();
    wide(data); // Reject malformed UTF-8 even in comments or unused text.
    if (data.starts_with("\xef\xbb\xbf")) data.erase(0, 3);
    Settings settings; std::set<std::string> keys;
    for (size_t begin = 0; begin < data.size();) {
        auto end = data.find('\n', begin); if (end == std::string::npos) end = data.size();
        auto line = std::string_view(data).substr(begin, end - begin); begin = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > 32768 || line.find('\r') != std::string_view::npos) invalid();
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) continue;
        const auto equal = line.find('='); if (equal == std::string_view::npos) invalid();
        const auto key = line.substr(0, equal), value = line.substr(equal + 1);
        if (!keys.insert(std::string(key)).second) invalid();
        if (key == "version") { if (value != "1") invalid(); }
        else if (conversion_field(settings.conversion, key, value)) {}
        else if (!options_only && key == "notifications") {
            if(value == "all") settings.notifications = NotificationMode::All;
            else if(value == "errors") settings.notifications = NotificationMode::Errors;
            else if(value == "none") settings.notifications = NotificationMode::None; else invalid();
        }
        else if (!options_only && key == "automatic_updates") { if (value != "0" && value != "1") invalid(); settings.automatic_updates = value == "1"; }
        else if (!options_only && key == "update_channel") settings.update_channel = wide(value);
        else if (!options_only && key == "output_directory") settings.output_directory = wide(value);
        else if (!options_only && key == "auto_show_progress") {
            if (value != "0" && value != "1") invalid(); settings.auto_show_progress = value == "1";
        } else if (!options_only && key.starts_with("wheel.")) {
            Category category = Category::Unsupported;
            for (auto candidate : {Category::Image, Category::Video, Category::Audio, Category::Document, Category::Spreadsheet, Category::Pdf, Category::Presentation})
                if (key.substr(6) == category_key(candidate)) category = candidate;
            if (category == Category::Unsupported || value.empty()) invalid();
            auto& formats = settings.wheels[category];
            for (size_t start = 0;;) {
                auto comma = value.find(',', start); auto token = value.substr(start, comma == std::string_view::npos ? comma : comma - start);
                if (token.empty()) invalid(); formats.push_back(parse_format(wide(token)));
                if (comma == std::string_view::npos) break; start = comma + 1;
            }
        } else invalid();
    }
    // An untouched 0.3.0 all-formats image wheel should discover HEIC on upgrade.
    // Newly saved configs carry OCR keys, so explicitly hiding HEIC stays respected.
    if (!options_only && !keys.contains("pdf_ocr") && !keys.contains("ocr_languages")) {
        auto found = settings.wheels.find(Category::Image);
        const std::vector<Format> previous{Format::Jpeg, Format::Png, Format::Bmp, Format::Avif, Format::Webp, Format::Tiff};
        if (found != settings.wheels.end() && found->second == previous) found->second.push_back(Format::Heic);
    }
    validate_settings(settings); return settings;
}
std::string serialize_options(const ConversionOptions& o) {
    validate_options(o);
    return std::string("version=1\nquality=") + (o.quality == QualityPreset::Compact ? "compact" : o.quality == QualityPreset::High ? "high" : "balanced") +
        "\nresources=" + (o.resources == ResourceMode::Economy ? "economy" : o.resources == ResourceMode::Fast ? "fast" : "balanced") +
        "\nimage_max_dimension=" + std::to_string(o.image_max_dimension) + "\nvideo_max_height=" + std::to_string(o.video_max_height) +
        "\naudio_bitrate_kbps=" + std::to_string(o.audio_bitrate_kbps) + "\npdf_dpi=" + std::to_string(o.pdf_dpi) +
        "\npdf_ocr=" + (o.pdf_ocr ? "1" : "0") + "\nocr_languages=" + o.ocr_languages +
        "\nvideo_acceleration=" + (o.video_acceleration == VideoAcceleration::Cpu ? "cpu" : "auto") +
        "\nimage_metadata=" + (o.image_metadata == ImageMetadata::PhotoDetails ? "photo_details" : o.image_metadata == ImageMetadata::CaptureDate ? "capture_date" : "remove") +
        "\nvideo_codec=" + (o.video_codec == VideoCodec::H264 ? "h264" : o.video_codec == VideoCodec::Hevc ? "hevc" : o.video_codec == VideoCodec::Av1 ? "av1" : "default") + "\n";
}
bool write_exclusive(const fs::path& path, const std::string& data, bool allow_collision) {
    Handle file{CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (allow_collision && (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)) return false;
        throw std::runtime_error("Не удалось создать файл настроек. Существующий файл не изменён.");
    }
    DWORD written = 0;
    if (!WriteFile(file.value, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written != data.size() || !FlushFileBuffers(file.value)) {
        CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE; DeleteFileW(path.c_str());
        throw std::runtime_error("Не удалось сохранить настройки на диск.");
    }
    return true;
}
}
void validate_options(const ConversionOptions& options) {
    if (options.quality != QualityPreset::Compact && options.quality != QualityPreset::Balanced && options.quality != QualityPreset::High) invalid();
    if (options.resources != ResourceMode::Economy && options.resources != ResourceMode::Balanced && options.resources != ResourceMode::Fast) invalid();
    if (options.video_acceleration != VideoAcceleration::Auto && options.video_acceleration != VideoAcceleration::Cpu) invalid();
    if (options.image_metadata != ImageMetadata::Remove && options.image_metadata != ImageMetadata::CaptureDate && options.image_metadata != ImageMetadata::PhotoDetails) invalid();
    if (options.video_codec < VideoCodec::Default || options.video_codec > VideoCodec::Av1) invalid();
    if (options.image_max_dimension > 32768 || options.video_max_height > 8640 || options.video_max_height == 1 ||
        (options.audio_bitrate_kbps && (options.audio_bitrate_kbps < 32 || options.audio_bitrate_kbps > 320)) || options.pdf_dpi < 36 || options.pdf_dpi > 600) invalid();
    if (options.ocr_languages.empty() || options.ocr_languages.size() > 11) invalid();
    std::set<std::string_view> languages;
    for (size_t start = 0;;) {
        const auto plus = options.ocr_languages.find('+', start);
        const auto token = std::string_view(options.ocr_languages).substr(start, plus == std::string::npos ? plus : plus - start);
        if ((token != "eng" && token != "rus" && token != "aze") || !languages.insert(token).second) invalid();
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
}
unsigned resource_threads(ResourceMode mode) {
    switch (mode) {
    case ResourceMode::Economy: return 1;
    case ResourceMode::Balanced: return 2;
    case ResourceMode::Fast: return std::clamp(std::thread::hardware_concurrency(), 1U, 8U);
    default: invalid();
    }
}
Settings load_settings(const fs::path& path) { return read_config(path, false, true); }
ConversionOptions load_options(const fs::path& path) { return read_config(path, true, false).conversion; }
void save_options(const fs::path& path, const ConversionOptions& options) { write_exclusive(path, serialize_options(options), false); }
void save_settings(const fs::path& path, const Settings& settings) {
    validate_settings(settings);
    auto text = serialize_options(settings.conversion) + "auto_show_progress=" + (settings.auto_show_progress ? "1\n" : "0\n") + "output_directory=" + narrow(settings.output_directory.wstring()) + "\n";
    text += "notifications=" + std::string(settings.notifications == NotificationMode::All ? "all" : settings.notifications == NotificationMode::Errors ? "errors" : "none") + "\n";
    text += "automatic_updates=" + std::string(settings.automatic_updates ? "1\n" : "0\n") + "update_channel=" + narrow(settings.update_channel) + "\n";
    for (const auto& [category, formats] : settings.wheels) {
        text += std::string("wheel.") + category_key(category) + "=";
        for (size_t i = 0; i < formats.size(); ++i) { if (i) text += ','; text += narrow(extension(formats[i]) + 1); }
        text += '\n';
    }
    if (text.size() > max_config_size) invalid();
    static std::atomic<unsigned long> serial{0};
    fs::path temporary; bool created = false;
    for (unsigned attempt = 0; attempt < 64 && !created; ++attempt) {
        temporary = path; temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial);
        created = write_exclusive(temporary, text, true);
    }
    if (!created) throw std::runtime_error("Не удалось выделить временный файл настроек.");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Не удалось заменить настройки. Предыдущий файл сохранён. Код: " + std::to_string(error));
    }
}
std::vector<Format> configured_formats(const fs::path& input, const Settings& settings) {
    validate_settings(settings);
    const auto found = settings.wheels.find(category(input));
    return found == settings.wheels.end() ? formats_for(input) : found->second;
}
}
