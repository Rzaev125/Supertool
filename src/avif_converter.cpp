#include "avif_converter.h"
#include "engines.h"
#include "image_converter.h"
#include <fstream>
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;
namespace {
// avifdec 1.4.2 has no JSON output. Check complete, pinned diagnostic lines,
// never substrings of the input filename or metadata. Fail closed if changed.
bool validate_decoded_avif(const fs::path& log) {
    std::ifstream stream(log);
    bool single = false, untransformed = false, srgb_transfer = false, srgb_primaries = false, profile = false, hdr = false;
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        single |= line == " * Frame:";
        untransformed |= line == " * Transformations: None";
        srgb_transfer |= line == " * Transfer Char. : 13" || line == " * Transfer Char. : 2";
        srgb_primaries |= line == " * Color Primaries: 1" || line == " * Color Primaries: 2";
        hdr |= line == " * Transfer Char. : 16" || line == " * Transfer Char. : 18";
        profile |= line.starts_with(" * ICC Profile    : Present (") && line.ends_with(" bytes)");
    }
    if (!stream.eof()) throw std::runtime_error("Не удалось прочитать сведения AVIF.");
    if (!single) throw std::runtime_error("Анимированные и многокадровые AVIF пока не поддерживаются.");
    if (!untransformed) throw std::runtime_error("Сначала примените поворот, отражение или кадрирование AVIF в редакторе.");
    if (hdr || (!profile && !(srgb_transfer && srgb_primaries)))
        throw std::runtime_error("Этот AVIF использует HDR или цветовое пространство без поддерживаемого профиля. Сначала преобразуйте его в sRGB.");
    return profile;
}
}
void convert_avif(const fs::path& input, const fs::path& output, Format format, const fs::path& work, const ConversionOptions& options) {
    validate_options(options);
    const auto threads = std::to_wstring(resource_threads(options.resources));
    if (fs::file_size(input) > 1024ULL * 1024 * 1024) throw std::runtime_error("Лимит исходного изображения для AVIF: 1 ГБ.");
    fs::path pixels = input;
    ImageColor color = ImageColor::ToSrgb;
    if (parse_format(input.extension().wstring()) == Format::Avif) {
        pixels = work / L"decoded.png";
        const auto log = work / L"avif-decode.log";
        run_engine(avif_module(false), {L"--jobs", threads, L"--depth", L"8", L"--size-limit", L"40000000",
            L"--dimension-limit", L"32768", L"--index", L"0", L"--", input.wstring(), pixels.wstring()}, log, 120000);
        if (validate_decoded_avif(log)) color = ImageColor::RequireProfile;
    }
    if (format == Format::Avif) {
        // Normalize via WIC first: one frame, size checks, BGRA alpha, no metadata.
        const auto normalized = work / L"normalized.png";
        convert_image(pixels, normalized, work / L"normalize.tmp", color, options);
        const auto quality = options.quality == QualityPreset::Compact ? L"60" : options.quality == QualityPreset::High ? L"95" : L"80";
        run_engine(avif_module(true), {L"--jobs", threads, L"--speed", L"6", L"--qcolor", quality, L"--qalpha", L"100",
            L"--depth", L"8", L"--yuv", L"444", L"--cicp", L"1/13/6", L"--no-overwrite",
            L"--ignore-exif", L"--ignore-xmp", L"--ignore-icc", L"--", normalized.wstring(), output.wstring()}, work / L"avif-encode.log", 300000);
    } else {
        // PNG keeps alpha; JPEG/BMP use the existing white-background blend.
        convert_image(pixels, output, work / L"image.tmp", color, options);
    }
}
}
