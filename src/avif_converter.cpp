#include "avif_converter.h"
#include "engines.h"
#include "image_converter.h"
#include "avif_info.h"
#include <stdexcept>

namespace shiftwheel {
namespace fs = std::filesystem;

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
        const auto info = inspect_decoded_avif(log);
        if (info.profile) color = ImageColor::RequireProfile;
        if (info.hdr != HdrTransfer::None) {
            const auto raw = work / L"hdr-decoded16.png", second_log = work / L"avif-decode16.log";
            run_engine(avif_module(false), {L"--jobs", threads, L"--depth", L"16", L"--size-limit", L"40000000",
                L"--dimension-limit", L"32768", L"--index", L"0", L"--", input.wstring(), raw.wstring()}, second_log, 120000);
            if (inspect_decoded_avif(second_log) != info) throw std::runtime_error("Изменились сведения HDR AVIF при декодировании.");
            pixels = work / L"hdr-sdr.png";
            hdr_to_sdr(raw, pixels, info.hdr);
        }
        // avifdec applies clap -> irot -> imir to PNG pixels and resets EXIF
        // orientation. WIC must not repeat those container transforms.
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
