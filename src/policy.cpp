#include "policy.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <numbers>
#include <stdexcept>
namespace shiftwheel {
static std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
int sector_at(float x, float y, float inner, float outer, int count) {
    float radius = std::hypot(x, y);
    if (count <= 0 || radius <= inner || radius > outer) return -1;
    const float circle = 2 * std::numbers::pi_v<float>;
    const float step = circle / static_cast<float>(count);
    float angle = std::atan2(x, -y) + step / 2;
    if (angle < 0) angle += circle;
    return static_cast<int>(angle / step) % count;
}
bool can_accept(bool shift, bool copy, std::size_t files, bool supported, int sector, bool busy, int sector_count) {
    return shift && copy && files > 0 && files <= 256 && supported && sector >= 0 && sector < sector_count && !busy;
}
Category category(const std::filesystem::path& path) {
    auto ext = lower(path.extension().wstring());
    auto in = [&](std::initializer_list<const wchar_t*> values) { return std::any_of(values.begin(), values.end(), [&](auto value) { return ext == value; }); };
    if (in({L".png", L".jpg", L".jpeg", L".bmp", L".avif", L".webp", L".tif", L".tiff", L".heic", L".heif", L".gif"})) return Category::Image;
    if (in({L".mp4", L".mkv", L".webm", L".mov", L".avi", L".m4v", L".wmv", L".mpeg", L".mpg"})) return Category::Video;
    if (in({L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wma", L".aiff", L".aif"})) return Category::Audio;
    if (in({L".docx", L".doc", L".odt", L".rtf"})) return Category::Document;
    if (in({L".xlsx", L".xls", L".ods"})) return Category::Spreadsheet;
    if (ext == L".pdf") return Category::Pdf;
    if (in({L".pptx", L".ppt", L".odp"})) return Category::Presentation;
    return Category::Unsupported;
}
bool supported_input(const std::filesystem::path& path) { return category(path) != Category::Unsupported; }
std::vector<Format> formats_for(const std::filesystem::path& path) {
    using enum Format;
    switch (category(path)) {
    case Category::Image: return {Jpeg, Png, Bmp, Avif, Webp, Tiff, Heic, Gif};
    case Category::Video: return {Mp4, Mkv, Webm, Mp3, Wav, Mov};
    case Category::Audio: return {Mp3, Wav, Flac, M4a, Ogg, Opus, Aac};
    case Category::Document: return {Pdf, Docx, Odt, Rtf, Txt};
    case Category::Spreadsheet: return {Pdf, Xlsx, Ods, Csv};
    case Category::Pdf: return {Png, Jpeg, Txt};
    case Category::Presentation: return {Pdf, Pptx, Odp, Png, Jpeg};
    default: return {};
    }
}
bool supports_conversion(const std::filesystem::path& path, Format format) {
    auto formats = formats_for(path);
    return std::find(formats.begin(), formats.end(), format) != formats.end();
}
std::vector<Format> available_formats(const std::filesystem::path& input, std::vector<Format> candidates,
                                    const std::function<bool(std::string_view)>& module_available) {
    const auto source_category = category(input);
    auto image_available = [&](Format format) {
        switch (format) {
        case Format::Avif: return module_available("avif");
        case Format::Heic: return module_available("heic");
        case Format::Webp: case Format::Gif: return module_available("ffmpeg");
        default: return true; // JPEG, PNG, BMP and TIFF use Windows codecs.
        }
    };
    switch (source_category) {
    case Category::Image:
        if (!image_available(parse_format(input.extension().wstring()))) return {};
        break;
    case Category::Video: case Category::Audio:
        if (!module_available("ffmpeg")) return {};
        break;
    case Category::Document: case Category::Spreadsheet: case Category::Presentation:
        if (!module_available("libreoffice")) return {};
        break;
    case Category::Pdf:
        // PDFium can extract existing text without the optional OCR module.
        if (!module_available("pdfium")) return {};
        break;
    default: return {};
    }
    const auto supported = formats_for(input);
    std::erase_if(candidates, [&](Format format) {
        if (std::find(supported.begin(), supported.end(), format) == supported.end()) return true;
        if (source_category == Category::Image) return !image_available(format);
        return source_category == Category::Presentation && (format == Format::Png || format == Format::Jpeg) &&
            !module_available("pdfium");
    });
    return candidates;
}
bool folder_output(const std::filesystem::path& path, Format format) {
    return ((category(path) == Category::Pdf || category(path) == Category::Presentation) && (format == Format::Png || format == Format::Jpeg)) ||
        (category(path) == Category::Spreadsheet && format == Format::Csv);
}
std::wstring quote_argument(std::wstring_view argument) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
const wchar_t* extension(Format format) {
    switch (format) {
    case Format::Jpeg: return L".jpg";
    case Format::Png: return L".png";
    case Format::Bmp: return L".bmp";
    case Format::Mp4: return L".mp4";
    case Format::Mkv: return L".mkv";
    case Format::Webm: return L".webm";
    case Format::Mp3: return L".mp3";
    case Format::Wav: return L".wav";
    case Format::Flac: return L".flac";
    case Format::M4a: return L".m4a";
    case Format::Ogg: return L".ogg";
    case Format::Pdf: return L".pdf";
    case Format::Docx: return L".docx";
    case Format::Odt: return L".odt";
    case Format::Rtf: return L".rtf";
    case Format::Txt: return L".txt";
    case Format::Xlsx: return L".xlsx";
    case Format::Ods: return L".ods";
    case Format::Csv: return L".csv";
    case Format::Avif: return L".avif";
    case Format::Webp: return L".webp";
    case Format::Tiff: return L".tiff";
    case Format::Opus: return L".opus";
    case Format::Aac: return L".aac";
    case Format::Mov: return L".mov";
    case Format::Heic: return L".heic";
    case Format::Pptx: return L".pptx";
    case Format::Odp: return L".odp";
    case Format::Gif: return L".gif";
    }
    return L"";
}
Format parse_format(std::wstring_view id) {
    auto value = lower(std::wstring(id));
    if (!value.empty() && value[0] != L'.') value = L"." + value;
    if (value == L".jpeg") value = L".jpg";
    if (value == L".tif") value = L".tiff";
    if (value == L".heif") value = L".heic";
    for (int i = 0; i <= static_cast<int>(Format::Gif); ++i)
        if (value == extension(static_cast<Format>(i))) return static_cast<Format>(i);
    throw std::runtime_error("Неизвестный выходной формат.");
}
const wchar_t* label(Format format) {
    static constexpr const wchar_t* labels[]{L"JPG", L"PNG", L"BMP", L"MP4", L"MKV", L"WebM", L"MP3", L"WAV", L"FLAC", L"M4A", L"OGG", L"PDF", L"DOCX", L"ODT", L"RTF", L"TXT", L"XLSX", L"ODS", L"CSV", L"AVIF", L"WebP", L"TIFF", L"Opus", L"AAC", L"MOV", L"HEIC", L"PPTX", L"ODP", L"GIF"};
    auto i = static_cast<unsigned>(format);
    return i < std::size(labels) ? labels[i] : L"?";
}
const wchar_t* caption(Format format,bool first_frame) {
    if(first_frame&&format!=Format::Gif&&format!=Format::Webp)return L"Первый кадр";
    switch (format) {
    case Format::Jpeg: return L"Белый фон";
    case Format::Png: return L"Без потерь";
    case Format::Bmp: return L"Без сжатия";
    case Format::Avif: return L"Компактное фото";
    case Format::Heic: return L"Фото HEVC";
    case Format::Webp: return L"Фото / анимация";
    case Format::Gif: return L"256 цветов";
    case Format::Tiff: return L"Без потерь";
    case Format::Mp4: case Format::Mkv: case Format::Mov: return L"H.264 · AAC";
    case Format::Webm: return L"VP9 · Opus";
    case Format::Mp3: return L"Аудио";
    case Format::Wav: return L"PCM 16 бит";
    case Format::Flac: return L"Без потерь";
    case Format::M4a: return L"AAC";
    case Format::Ogg: return L"Vorbis";
    case Format::Opus: return L"Аудио";
    case Format::Aac: return L"AAC";
    case Format::Pdf: return L"Все страницы";
    case Format::Txt: return L"Только текст";
    case Format::Csv: return L"Все листы";
    case Format::Xlsx: case Format::Ods: return L"Таблица";
    case Format::Pptx: case Format::Odp: return L"Презентация";
    default: return L"Документ";
    }
}
std::filesystem::path output_candidate(const std::filesystem::path& input, Format format, unsigned suffix) {
    auto stem = input.stem().wstring();
    bool folder = folder_output(input, format);
    if (folder) stem += L"-" + std::wstring(extension(format) + 1);
    else if (lower(input.extension().wstring()) == extension(format) ||
        (format == Format::Tiff && lower(input.extension().wstring()) == L".tif") ||
        (format == Format::Heic && lower(input.extension().wstring()) == L".heif") ||
        (format == Format::Jpeg && lower(input.extension().wstring()) == L".jpeg")) stem += L"-converted";
    if (suffix) stem += L" (" + std::to_wstring(suffix) + L")";
    return input.parent_path() / (stem + (folder ? L"" : extension(format)));
}
void Gesture::begin(int x, int y, bool eligible) { start_x_ = x; start_y_ = y; down_ = eligible; fired_ = false; }
bool Gesture::move(int x, int y, bool shift, int threshold_x, int threshold_y) {
    if (!down_ || fired_ || !shift) return false;
    if (std::abs(x - start_x_) <= threshold_x && std::abs(y - start_y_) <= threshold_y) return false;
    fired_ = true;
    return true;
}
void Gesture::shift_released() { fired_ = false; }
void Gesture::end() { down_ = false; fired_ = false; }
}
